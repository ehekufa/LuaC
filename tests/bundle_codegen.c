#include "luac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Explicit sources are compiled even when their require name is computed. */
int main(int argc, char **argv) {
    enum { COUNT = 30 };
    LuacModuleSource modules[COUNT + 1];
    char names[COUNT][32], filenames[COUNT][48], sources[COUNT][48];
    const char *main_source =
        "local total = 0\n"
        "for i = 1, 30 do\n"
        "  local module = 'scripts.mod' .. i\n"
        "  total = total + require(module)\n"
        "end\n"
        "assert(total == 465)\n"
        "assert(require('quote\\\"\\n') == 99)\n"
        "print('Compiled bundle: OK')\n";
    char *error = NULL, *generated;
    FILE *file;
    clock_t before;
    int i;
    if (argc != 2) return 2;
    for (i = 0; i < COUNT; ++i) {
        snprintf(names[i], sizeof(names[i]), "scripts.mod%d", i + 1);
        snprintf(filenames[i], sizeof(filenames[i]), "scripts/mod%d.lua", i + 1);
        snprintf(sources[i], sizeof(sources[i]), "return %d\n", i + 1);
        modules[i].name = names[i]; modules[i].filename = filenames[i]; modules[i].source = sources[i];
    }
    modules[COUNT].name = "quote\"\n";
    modules[COUNT].filename = "quoted.lua";
    modules[COUNT].source = "return 99\n";
    before = clock();
    generated = luac_compile_bundle_c(main_source, "main.lua", modules, COUNT + 1, &error);
    if (!generated) { fprintf(stderr, "bundle compile: %s\n", error ? error : "unknown error"); luac_free(error); return 1; }
    printf("Generated %d modules in %.3f s\n", COUNT + 1, (double)(clock() - before) / CLOCKS_PER_SEC);
    file = fopen(argv[1], "wb");
    if (!file) { perror(argv[1]); luac_free(generated); return 1; }
    {
        size_t length = strlen(generated);
        int failed = fwrite(generated, 1, length, file) != length;
        if (fclose(file)) failed = 1;
        if (failed) { perror("write generated C"); luac_free(generated); return 1; }
    }
    luac_free(generated);

    modules[1].name = modules[0].name;
    generated = luac_compile_bundle_c(main_source, "main.lua", modules, COUNT, &error);
    if (generated || !error || !strstr(error, "duplicate module")) { fprintf(stderr, "duplicate module accepted\n"); return 1; }
    luac_free(error); error = NULL;
    modules[1].name = names[1]; modules[1].source = "local =";
    generated = luac_compile_bundle_c(main_source, "main.lua", modules, COUNT, &error);
    if (generated || !error || !strstr(error, filenames[1])) { fprintf(stderr, "missing module error: %s\n", error ? error : "none"); return 1; }
    luac_free(error); error = NULL;
    generated = luac_compile_bundle_c("local =", "broken-main.lua", NULL, 0, &error);
    if (generated || !error || !strstr(error, "broken-main.lua")) { fprintf(stderr, "missing main error\n"); return 1; }
    luac_free(error); error = NULL;
    generated = luac_compile_bundle_c(main_source, "main.lua", NULL, 1, &error);
    if (generated || !error) { fprintf(stderr, "NULL bundle accepted\n"); return 1; }
    luac_free(error);
    return 0;
}
