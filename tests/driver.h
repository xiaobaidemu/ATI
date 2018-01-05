#pragma once

#define UNITTEST

#include <functional>
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


#define BEGIN_TESTS_DECLARATION         test_t __tests__[] = {

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
