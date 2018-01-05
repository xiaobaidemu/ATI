#include "driver.h"
#include <common/timer.h>
#include <cstring>

extern test_t __tests__[];

int main(int argc, char** argv)
{
    for (int i = 0; __tests__[i].name != nullptr; ++i) {
        const test_t& test = __tests__[i];

        const char* fn1 = strrchr(test.filename, '/');
        const char* fn2 = strrchr(test.filename, '\\');
        const char* fn = (fn1 > fn2) ? fn1 : fn2;
        if (fn == nullptr) {
            fn = "<unknown>";
        }
        else {
            ++fn;
        }

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
    return 0;
}
