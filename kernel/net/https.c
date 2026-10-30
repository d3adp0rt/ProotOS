#include "net.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/debug.h"
#include "mbedtls/error.h"
#include "kernel.h"
#include "timer.h"

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

int net_https_get(const char *host, const char *path, char *out, size_t cap) {
    uint8_t ip[4];
    if (net_dns_query(host, ip) != 0) return -1;
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
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    static const int suites[] = { MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, 0 };
    mbedtls_ssl_conf_ciphersuites(&conf, suites);
    mbedtls_ssl_conf_dbg(&conf, tls_dbg, 0);
    mbedtls_debug_set_threshold(0);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0) { kprintf("[https] setup %d\n", ret); goto fail; }
    mbedtls_ssl_set_hostname(&ssl, host);
    mbedtls_ssl_set_bio(&ssl, 0, bio_send, bio_recv, 0);

    ret = mbedtls_ssl_handshake(&ssl);
    if (ret != 0) {
        char eb[128];
        mbedtls_strerror(ret, eb, sizeof(eb));
        kprintf("[https] handshake %d: %s\n", ret, eb);
        goto fail;
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
    size_t blen = total - (size_t)(body - out);
    memmove(out, body, blen);
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
