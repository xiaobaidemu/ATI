#pragma once

#ifndef UNITTEST
#define UNITTEST
#endif

#include <common/timer.h>
#include <functional>
#include <cstring>
#include <cstdio>

typedef std::function<void()> test_function_t;
typedef std::function<void(const void*)> parameter_test_function_t;

struct test_t
{
    const char* filename;
    const char* name;
    test_function_t test_function;
    parameter_test_function_t parameter_test_function;
    void* parameter;
};


#define BEGIN_TESTS_DECLARATION(name)   \
    extern test_t __tests_##name##__[]; \
    void __do_test__##name() { __do_test__(__tests_##name##__); } \
    test_t __tests_##name##__[] = { \
        { __FILE__, nullptr, nullptr, nullptr, nullptr },

#define DECLARE_TEST(fn)                { __FILE__, #fn, fn, nullptr, nullptr },
#define DECLARE_PARAMETER_TEST(fn, p)   { __FILE__, #fn, nullptr, fn, p },

#define END_TESTS_DECLARATION           { __FILE__, nullptr, nullptr, nullptr, nullptr } };



#define TEST_FAIL() \
    do { \
        throw false; \
    } while(false)

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            printf("\033[31;1mTEST_ASSERT(%s) failed.\n%s (line %d)\033[0m\n", #expr, __FILE__, __LINE__); \
            fflush(stdout); \
            TEST_FAIL(); \
        } \
    } while(false)

#ifndef UNITTEST_NO_DOTEST

static void __do_test__(const test_t* tests)
{
    const auto getfilename = [](const char* filepath) {
        const char* fn1 = strrchr(filepath, '/');
        const char* fn2 = strrchr(filepath, '\\');
        const char* fn = (fn1 > fn2) ? fn1 : fn2;
        if (fn == nullptr) {
            fn = "<unknown>";
        }
        else {
            ++fn;
        }
        return fn;
    };

    const char* fn = getfilename(tests[0].filename);
    printf("\n\033[33;1m======================== %s ========================\033[0m\n", fn);

    for (int i = 1; tests[i].name != nullptr; ++i) {
        const test_t& test = tests[i];

        static const int STATUS_SUCCESSFUL = 1;
        static const int STATUS_SKIPPED = 2;
        static const int STATUS_FAILED = 3;

        timer tmr;

        int status = STATUS_SUCCESSFUL;
        printf("\033[34;1m[%s] %s: starts.\033[0m\n", fn, test.name);
        try {
            if (test.test_function) {
                test.test_function();
            }
            else if (test.parameter_test_function != nullptr) {
                test.parameter_test_function(test.parameter);
            }
            else {
                status = STATUS_SKIPPED;
            }
        }
        catch (...) {
            status = STATUS_FAILED;
        }

        if (status == STATUS_SUCCESSFUL) {
            printf("\033[32;1m[%s] %s: passed (%.3lf ms)\033[0m\n\n", fn, test.name, tmr.elapsed() * 1000);
        }
        else if (status == STATUS_FAILED) {
            printf("\033[31;1m[%s] %s: failed (%.3lf ms)\033[0m\n\n", fn, test.name, tmr.elapsed() * 1000);
        }
        else {
            printf("\033[33;1m[%s] %s: skipped (no test function)\033[0m\n\n", fn, test.name);
        }

    }
}

#endif
