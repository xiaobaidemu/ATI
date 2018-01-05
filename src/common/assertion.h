#pragma once

#include "log.h"
#include <cerrno>
#include <cstring>

#define CCALL(expr) \
    (([](const decltype(expr) __result) -> decltype(expr) { \
        if (__result < 0) { \
            const int __error = errno; \
            ERROR("CCALL() failed: %s returns %lld, errno = %d (%s), %s line %d\n", \
                  #expr, (long long)__result, __error, strerror(__error), __FILE__, __LINE__); \
            throw 0; \
        }\
        return __result; \
    })(expr))

#define ASSERT_RESULT(expr) \
    do { \
        const auto __expr = (expr); \
        if (__expr < 0) { \
            ERROR("ASSERT_RESULT() failed: %s = %lld, %s line %d\n", \
                  #expr, (long long)__expr, __FILE__, __LINE__); \
            throw 0; \
        }\
    } while(false)

#define ASSERT(expr) \
    do { \
        if (!(bool)(expr)) { \
            ERROR("ASSERT(%s) failed: %s line %d\n", #expr, __FILE__, __LINE__); \
            throw 0; \
        }\
    } while(false)
