#include "test.h"

#include <common/lock.h>
#include <thread>
#include <vector>

void test_lock_add()
{
    const int THREAD_COUNT = 100;
    const int ROUND_IN_THREAD = 10000;
    const int EXPECTED_SUM = THREAD_COUNT * ROUND_IN_THREAD;

    std::vector<std::thread> threads(THREAD_COUNT);

    volatile int sum = 0;
    lock lck(true);

    for (auto& thread : threads) {
        thread = std::thread([&sum, &lck]() {
            for (int i = 0; i < ROUND_IN_THREAD; ++i) {
                lck.acquire();
                ++sum;
                lck.release();
            }
        });
    }

    TEST_ASSERT(sum == 0);

    lck.release();
    for (auto& thread : threads) {
        thread.join();
    }

    printf("sum: expected %d, actually %d.\n", EXPECTED_SUM, sum);
    TEST_ASSERT(sum == EXPECTED_SUM);
}


void test_is_locked_default()
{
    lock lck;
    TEST_ASSERT(!lck.is_locked());
    lck.acquire();
    TEST_ASSERT(lck.is_locked());
    lck.release();
    TEST_ASSERT(!lck.is_locked());
}



BEGIN_TESTS_DECLARATION(test_lock)
DECLARE_TEST(test_lock_add)
DECLARE_TEST(test_is_locked_default)
END_TESTS_DECLARATION
