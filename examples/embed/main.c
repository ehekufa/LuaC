#include <luac.h>
#include <stdio.h>

int main(void) {
    char *error = NULL;
    char *generated = luac_compile_c("print('embedded')", "embedded.lua", &error);
    if (!generated) {
        fprintf(stderr, "%s\n", error ? error : "compilation failed");
        luac_free(error);
        return 1;
    }
    luac_free(generated);
    puts("Embedded LuaC compiler: OK");
    return 0;
}
