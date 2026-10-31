// Minimal test helper: no framework dependency.
#pragma once
#include <cstdio>
#include <cstdlib>

inline int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)
#define TEST_MAIN_END()                                                          \
    do {                                                                         \
        if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures); \
        else std::printf("all checks passed\n");                                 \
        return g_failures ? 1 : 0;                                               \
    } while (0)
