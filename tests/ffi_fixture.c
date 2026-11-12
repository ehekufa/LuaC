#include <stdint.h>
#include <string.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API int64_t luac_test_add64(int64_t a, int64_t b) { return a + b; }
API double luac_test_mul(double a, double b) { return a * b; }
API size_t luac_test_length(const char *s) { return s ? strlen(s) : 0; }
API int32_t luac_test_abs(int32_t n) { return n < 0 ? -n : n; }
