/*
 * ProotOS — Lua 5.4 runner (M11).
 *
 * Loads a script passed on the command line or runs a small line-oriented
 * REPL on stdin. The io/os/package libraries are intentionally absent.
 */
#include "libc.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static void open_libs(lua_State *L) {
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_DBLIBNAME, luaopen_debug},
        {0, 0},
    };
    for (const luaL_Reg *l = libs; l->func; l++) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
}

static int report(lua_State *L, int status) {
    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        printf("lua: %s\n", msg ? msg : "(error)");
        lua_pop(L, 1);
    }
    return status;
}

static int run_line(lua_State *L, const char *line, int len) {
    char buf[1024];
    const char *src = line;
    if (len > 0 && line[0] == '=') {
        int n = snprintf(buf, sizeof(buf), "return %s", line + 1);
        if (n > 0 && n < (int)sizeof(buf)) {
            src = buf;
            len = n;
        }
    }
    int status = luaL_loadbuffer(L, src, (size_t)len, "=stdin");
    if (status == LUA_OK) {
        status = lua_pcall(L, 0, LUA_MULTRET, 0);
        int nres = lua_gettop(L);
        for (int i = 1; i <= nres; i++) {
            const char *s = lua_tostring(L, i);
            if (s)
                printf("%s\n", s);
        }
        lua_settop(L, 0);
    }
    return report(L, status);
}

int main(int argc, char **argv) {
    lua_State *L = luaL_newstate();
    if (!L) {
        printf("lua: cannot create state\n");
        return 1;
    }
    open_libs(L);
    if (argc > 1) {
        if (report(L, luaL_loadfile(L, argv[1])) == LUA_OK)
            report(L, lua_pcall(L, 0, LUA_MULTRET, 0));
        lua_close(L);
        return 0;
    }
    printf("Lua %s on ProotOS (io/os/package disabled)\n", LUA_VERSION_MAJOR "." LUA_VERSION_MINOR);
    int infd = 0;
    if (fcntl(0, F_GETFL, 0) < 0)
        infd = open("/dev/kbd", 0, 0);
    char line[1024];
    int len = 0;
    for (;;) {
        printf("lua> ");
        len = 0;
        for (;;) {
            char c;
            long n = read(infd, &c, 1);
            if (n == 0) {
                sleep_ms(10);
                continue;
            }
            if (n < 0)
                return 0;
            if (c == '\n' || c == '\r') {
                putchar('\n');
                break;
            }
            if (c == 8 || c == 127) {
                if (len > 0) { len--; printf("\b \b"); }
                continue;
            }
            if (c >= 32 && len < (int)sizeof(line) - 1) {
                line[len++] = c;
                putchar(c);
            }
        }
        line[len] = 0;
        if (len)
            run_line(L, line, len);
    }
}
