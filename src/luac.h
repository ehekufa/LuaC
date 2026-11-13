#ifndef LUAC_PUBLIC_H
#define LUAC_PUBLIC_H

#include <stddef.h>

#if defined(_WIN32) && defined(LUAC_BUILD_SHARED)
#  define LUAC_API __declspec(dllexport)
#elif defined(_WIN32) && defined(LUAC_USE_SHARED)
#  define LUAC_API __declspec(dllimport)
#else
#  define LUAC_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Compile LuaC source to a standalone C translation unit.
 * The returned string and error must be released with luac_free().
 * The generated C still needs runtime.h when it is compiled. */
LUAC_API char *luac_compile_c(const char *source, const char *filename,
                              char **error_message);

/* Returns 1 for valid syntax, 0 for invalid input. */
LUAC_API int luac_check(const char *source, const char *filename,
                        char **error_message);

/* Optional in-process project runtime, enabled with LUAC_EMBEDDED_RUNTIME.
 * Runs main.lua and disk requires without a host C toolchain. Frame results
 * are JSON containing output, error, entities, camera and overlay texts. */
LUAC_API void *luac_project_open(const char *root_path, char **error_message);
LUAC_API char *luac_project_step(void *project, double dt);
LUAC_API void luac_project_touch(void *project, int id, double x, double y, int down);
LUAC_API void luac_project_close(void *project);

LUAC_API const char *luac_version(void);
LUAC_API void luac_free(void *memory);

#ifdef __cplusplus
}
#endif
#endif
