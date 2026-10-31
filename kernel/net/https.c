#include "net.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/debug.h"
#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"
#include "kernel.h"
#include "timer.h"
#include "initramfs.h"
#include "mm.h"

static void tls_dbg(void *ctx, int level, const char *file, int line, const char *str) {
    (void)ctx; (void)level; (void)file; (void)line;
    kprintf("[tls] %s", str);
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    (void)ctx;
    int n = tcp_conn_write(buf, len);
    return n < 0 ? MBEDTLS_ERR_SSL_WANT_WRITE : n;
}
static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    (void)ctx;
    int n = tcp_conn_read(buf, len);
    return n < 0 ? MBEDTLS_ERR_SSL_WANT_READ : n;
}

static char g_tls_host[64];
static uint8_t g_tls_ip[4];
static int g_tls_cached;

/* Trust anchors are unpacked from the initramfs cpio. The bundle is a
 * concatenation of PEM certificates (Mozilla CA list). */
static mbedtls_x509_crt g_ca;
static int g_ca_state; /* 0 = not tried, 1 = ready, -1 = unavailable */

static int ca_store_ready(void) {
    if (g_ca_state > 0)
        return 1;
    if (g_ca_state < 0)
        return 0;

    const void *data = 0;
    size_t size = 0;
    if (initramfs_find("etc/ssl/certs/ca-certificates.crt", &data, &size) != 0 || size == 0) {
        kprintf("[https] no CA store; certificate verification disabled\n");
        g_ca_state = -1;
        return 0;
    }
    unsigned char *pem = kmalloc(size + 1);
    if (!pem) {
        kprintf("[https] CA store: out of memory\n");
        g_ca_state = -1;
        return 0;
    }
    memcpy(pem, data, size);
    pem[size] = 0;
    mbedtls_x509_crt_init(&g_ca);
    int ret = mbedtls_x509_crt_parse(&g_ca, pem, size + 1);
    kfree(pem);
    if (ret < 0) {
        kprintf("[https] CA parse failed %d; verification disabled\n", ret);
        mbedtls_x509_crt_free(&g_ca);
        g_ca_state = -1;
        return 0;
    }
    kprintf("[https] CA store loaded: %llu bytes, %d skipped\n",
            (unsigned long long)size, ret);
    g_ca_state = 1;
    return 1;
}

int net_https_get(const char *host, const char *path, char *out, size_t cap) {
    uint8_t ip[4];
    if (g_tls_cached && strcmp(host, g_tls_host) == 0) {
        memcpy(ip, g_tls_ip, 4);
    } else {
        if (net_dns_query(host, ip) != 0) return -1;
    }
    if (tcp_connect(ip, 443) != 0) { kprintf("[https] TCP connect failed\n"); return -1; }

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);

    static const unsigned char pers[] = "prootos-tls";
    int ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, pers, sizeof(pers) - 1);
    if (ret != 0) { kprintf("[https] drbg seed %d\n", ret); goto fail; }
    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) { kprintf("[https] config %d\n", ret); goto fail; }
    int verify = ca_store_ready();
    if (verify) {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, &g_ca, 0);
    } else {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    static const int suites[] = { MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
                                   MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
                                   MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256, 0 };
    mbedtls_ssl_conf_ciphersuites(&conf, suites);
    mbedtls_ssl_conf_dbg(&conf, tls_dbg, 0);
    mbedtls_debug_set_threshold(0);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0) { kprintf("[https] setup %d\n", ret); goto fail; }
    mbedtls_ssl_set_hostname(&ssl, host);
    mbedtls_ssl_set_bio(&ssl, 0, bio_send, bio_recv, 0);

    ret = mbedtls_ssl_handshake(&ssl);
    if (ret == 0) {
        strcpy(g_tls_host, host);
        g_tls_host[sizeof(g_tls_host) - 1] = 0;
        memcpy(g_tls_ip, ip, 4);
        g_tls_cached = 1;
    }
    if (ret != 0) {
        char eb[128];
        mbedtls_strerror(ret, eb, sizeof(eb));
        kprintf("[https] handshake %d: %s\n", ret, eb);
        if (verify && ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
            uint32_t vr = mbedtls_ssl_get_verify_result(&ssl);
            char info[256];
            mbedtls_x509_crt_verify_info(info, sizeof(info), "  ", vr);
            kprintf("[https] verify: %s", info);
        }
        goto fail;
    }
    if (verify) {
        uint32_t vr = mbedtls_ssl_get_verify_result(&ssl);
        if (vr != 0) {
            char info[256];
            mbedtls_x509_crt_verify_info(info, sizeof(info), "  ", vr);
            kprintf("[https] verify failed: %s", info);
        }
    }

    char req[256];
    int rl = ksnprintf(req, sizeof(req),
                       "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    ret = mbedtls_ssl_write(&ssl, (const unsigned char *)req, (size_t)rl);
    if (ret != rl) { kprintf("[https] write %d\n", ret); goto fail; }

    size_t total = 0;
    for (;;) {
        if (total >= cap) break;
        int n = mbedtls_ssl_read(&ssl, (unsigned char *)out + total, cap - total);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (n <= 0) break;
        total += (size_t)n;
    }
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    tcp_conn_close();
    timer_sleep_ms(200);
    if (!total) return -1;
    char *body = 0;
    for (size_t i = 0; i + 3 < total; i++) {
        if (out[i]=='\r' && out[i+1]=='\n' && out[i+2]=='\r' && out[i+3]=='\n') {
            body = out + i + 4; break;
        }
    }
    if (!body) return -1;
    size_t hlen = (size_t)(body - out);
    size_t blen = total - hlen;

    /* Honor Content-Length when present so a short TLS/TCP read is reported
     * as an error instead of being mistaken for a complete body. */
    long want = -1;
    for (size_t i = 0; i + 16 < hlen; i++) {
        if ((out[i] == 'C' || out[i] == 'c') &&
            (out[i+1] == 'o' || out[i+1] == 'O') &&
            (out[i+2] == 'n' || out[i+2] == 'N') &&
            (out[i+3] == 't' || out[i+3] == 'T') &&
            (out[i+4] == 'e' || out[i+4] == 'E') &&
            (out[i+5] == 'n' || out[i+5] == 'N') &&
            (out[i+6] == 't' || out[i+6] == 'T') &&
            out[i+7] == '-' &&
            (out[i+8] == 'L' || out[i+8] == 'l') &&
            (out[i+9] == 'e' || out[i+9] == 'E') &&
            (out[i+10] == 'n' || out[i+10] == 'N') &&
            (out[i+11] == 'g' || out[i+11] == 'G') &&
            (out[i+12] == 't' || out[i+12] == 'T') &&
            (out[i+13] == 'h' || out[i+13] == 'H') &&
            out[i+14] == ':') {
            size_t j = i + 15;
            while (j < hlen && (out[j] == ' ' || out[j] == '\t')) j++;
            long v = 0; int digits = 0;
            while (j < hlen && out[j] >= '0' && out[j] <= '9') { v = v * 10 + (out[j] - '0'); j++; digits++; }
            if (digits) want = v;
            break;
        }
    }
    memmove(out, body, blen);
    if (want >= 0 && (size_t)want != blen) {
        kprintf("[https] short body: %llu/%ld bytes\n", (unsigned long long)blen, want);
        return -2;
    }
    kprintf("[https] %s %s: %llu bytes\n", host, path, (unsigned long long)blen);
    return (int)blen;

fail:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    tcp_conn_close();
    timer_sleep_ms(200);
    return -1;
}
