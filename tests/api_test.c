#include "luac.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    char *error = NULL;
    char *c;
    if (!luac_check("local x = 42", "api.lua", &error)) {
        fprintf(stderr, "%s\n", error ? error : "check failed");
        luac_free(error);
        return 1;
    }
    c = luac_compile_c("print('library OK')", "api.lua", &error);
    if (!c || !strstr(c, "lc_jit_main")) {
        fprintf(stderr, "%s\n", error ? error : "compile failed");
        luac_free(c); luac_free(error);
        return 1;
    }
    if (luac_check("local =", "bad.lua", &error)) {
        luac_free(c);
        return 1;
    }
    luac_free(error);
    luac_free(c);
    puts("C API: OK");
    return 0;
}
