#include "test.h"
#include <common/rundown_protection.h>
#include <common/timer.h>

#include <vector>
#include <thread>
#include <atomic>
#include <sched.h>
#include <random>

using std::vector;
using std::thread;
using std::atomic_int;
using std::atomic_bool;
using std::atomic_flag;


void test_basic_1()
{
    bool success;
    volatile int value = 0;
    rundown_protection rundown;

    rundown.register_callback([&value]() mutable {
        TEST_ASSERT(value == 0);
        value = 1;
    });
    TEST_ASSERT(value == 0);

    bool need_release;
    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(need_release);
    TEST_ASSERT(success);
    TEST_ASSERT(value == 0);

    rundown.shutdown();
    TEST_ASSERT(value == 0);

    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(!success);
    TEST_ASSERT(need_release);
    rundown.release();
    TEST_ASSERT(value == 0);

    // now callback is invoked in register_callback()
    rundown.release();
    TEST_ASSERT(value == 1);

    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(!need_release);
    TEST_ASSERT(!success);
    TEST_ASSERT(value == 1);
}


void test_basic_2()
{
    bool success;
    volatile int value = 0;
    rundown_protection rundown;

    bool need_release;
    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(need_release);
    TEST_ASSERT(success);
    TEST_ASSERT(value == 0);

    rundown.shutdown();
    TEST_ASSERT(value == 0);

    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(!success);
    TEST_ASSERT(need_release);
    rundown.release();

    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(!success);
    TEST_ASSERT(need_release);
    rundown.release();

    // now callback is invoked in register_callback()
    rundown.register_callback([&value]() mutable {
        TEST_ASSERT(value == 0);
        value = 1;
    });
    TEST_ASSERT(value == 0);

    rundown.release();
    TEST_ASSERT(value == 1);
}


void test_basic_3()
{
    bool success;
    volatile int value = 0;
    rundown_protection rundown;

    bool need_release;
    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(need_release);
    TEST_ASSERT(success);
    TEST_ASSERT(value == 0);

    rundown.release();
    TEST_ASSERT(value == 0);

    rundown.register_callback([&value]() mutable {
        TEST_ASSERT(value == 0);
        value = 1;
    });
    TEST_ASSERT(success);
    TEST_ASSERT(value == 0);

    // now callback is invoked in shutdown()
    rundown.shutdown();
    TEST_ASSERT(value == 1);

    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(!need_release);
    TEST_ASSERT(!success);
}


void test_basic_4()
{
    bool success;
    volatile int value = 0;
    rundown_protection rundown;

    bool need_release;
    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(need_release);
    TEST_ASSERT(success);
    TEST_ASSERT(value == 0);

    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(need_release);
    TEST_ASSERT(success);
    TEST_ASSERT(value == 0);

    rundown.release();
    TEST_ASSERT(value == 0);

    rundown.register_callback([&value]() mutable {
        TEST_ASSERT(value == 0);
        value = 1;
    });
    TEST_ASSERT(value == 0);

    // now callback is invoked in shutdown()
    rundown.shutdown();
    TEST_ASSERT(value == 0);

    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(!success);
    TEST_ASSERT(need_release);
    rundown.release();

    rundown.release();
    TEST_ASSERT(value == 1);

    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(!need_release);
    TEST_ASSERT(!success);
}


void test_multithread_1()
{
    const int THREAD_COUNT = 32;
    const int ATTEMPT_PER_THREAD = 100;

    rundown_protection rundown;
    volatile int value = 0;

    vector<thread*> threads(THREAD_COUNT);
    for (thread*& thr : threads) {
        thr = new thread([&]() {
            for (int i = 0; i < ATTEMPT_PER_THREAD; ++i) {
                bool need_release;
                bool success = rundown.try_acquire(&need_release);
                TEST_ASSERT(need_release);
                TEST_ASSERT(success);
            }
        });
    }

    for (thread* thr : threads) {
        thr->join();
        delete thr;
    }

    bool success;
    
    rundown.register_callback([&]() {
        TEST_ASSERT(value == 0);
        value = 1;
    });

    rundown.shutdown();

    bool need_release;
    success = rundown.try_acquire(&need_release);
    TEST_ASSERT(!success);
    TEST_ASSERT(need_release);
    rundown.release();

    for (int i = 0; i < THREAD_COUNT * ATTEMPT_PER_THREAD; ++i) {
        TEST_ASSERT(value == 0);
        rundown.release();
    }
    TEST_ASSERT(value == 1);
}


void test_multithread_2()
{
    const int THREAD_COUNT = 64;

    rundown_protection rundown;
    atomic_int succCnt(0);
    atomic_bool start_event(false);
    atomic_bool stop_event(false);

    vector<thread*> threads(THREAD_COUNT);
    for (thread*& thr : threads) {
        thr = new thread([&]() {
            while (!start_event) sched_yield();

            bool success = true;
            while (success) {
                bool need_release;
                success = rundown.try_acquire(&need_release);
                if (success) ++succCnt;
                else {
                    if (need_release) {
                        rundown.release();
                    }
                }
            }

            while (!stop_event) {
                bool need_release;
                success = rundown.try_acquire(&need_release);
                TEST_ASSERT(!success);
                if (need_release) {
                    rundown.release();
                }
                sched_yield();
            }
        });
    }

    TEST_ASSERT(succCnt == 0);
    start_event.store(true);

    rundown.register_callback([&]() {
        TEST_ASSERT(succCnt == 0);
        succCnt = -1;
    });

    // Sleep for some time (2ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    printf("try_acquire() success count ~= %d\n", (int)succCnt);

    rundown.shutdown();

    while (succCnt >= 0) {
        if (succCnt > 0) {
            --succCnt;
            rundown.release();
        }
    }
    TEST_ASSERT(succCnt == -1);

    stop_event.store(true);

    for (thread* thr : threads) {
        thr->join();
        delete thr;
    }
}


void test_multithread_3()
{
    const int THREAD_COUNT = 64;

    rundown_protection rundown;
    atomic_int totSuccCnt(0);
    atomic_bool start_event(false);
    atomic_bool shutdown_flag(false);

    vector<thread*> threads(THREAD_COUNT);
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        threads[i] = new thread([i, &start_event, &rundown, &totSuccCnt, &shutdown_flag]() {

            std::mt19937_64 rd(/*seed*/i * i * i);
            const std::mt19937_64::result_type threshold = rd.min() + (rd.max() - rd.min()) / 1000000;
            std::function<bool()> rand_stop = [&rd, threshold]() {
                return (rd() < threshold);
            };

            while (!start_event) sched_yield();

            bool success = true;
            while (!rand_stop() && success) {
                bool need_release;
                success = rundown.try_acquire(&need_release);
                if (success) {
                    ++totSuccCnt;
                }
                else {
                    if (need_release) rundown.release();
                }
                sched_yield();
            }

            success = rundown.shutdown();

            if (success) {

                TEST_ASSERT(shutdown_flag.exchange(true) == false);
                rundown.register_callback([&]() {
                    TEST_ASSERT(totSuccCnt == 0);
                    totSuccCnt.store(-1);
                });

                printf("try_acquire() total success count ~= %d\n", (int)totSuccCnt);
                timer tmr;
                while (totSuccCnt >= 0) {
                    if (totSuccCnt > 0) {
                        --totSuccCnt;
                        rundown.release();
                    }
                }
                printf("try_acquire() all released: %.3lf ms\n", tmr.elapsed() * 1000);

                TEST_ASSERT(totSuccCnt == -1);
            }
            else {
                while (totSuccCnt != -1) {
                    sched_yield();
                }
            }
        });
    }

    TEST_ASSERT(totSuccCnt == 0);
    start_event.store(true);
    

    for (thread* thr : threads) {
        thr->join();
        delete thr;
    }
    TEST_ASSERT(totSuccCnt == -1);

}

void test_acquire_speed()
{
    const int ACQUIRE_COUNT = 1000000000;
    rundown_protection rundown;
    timer tmr;
    for (int i = 0; i < ACQUIRE_COUNT; ++i) {
        bool need_release;
        rundown.try_acquire(&need_release);
    }

    const double elapsed = tmr.elapsed();
    printf("(single thread) try_acquire() %d times: %.3lf sec (%.2lf ns/request)\n",
           ACQUIRE_COUNT, elapsed, elapsed * 1000 * 1000 * 1000 / ACQUIRE_COUNT);
}

void test_acquire_speed_mt()
{
    const int THREAD_COUNT = 4;
    const int ACQUIRE_COUNT = 100000000;
    static_assert(ACQUIRE_COUNT % THREAD_COUNT == 0, "it should be accurate!");

    std::vector<thread*> threads(THREAD_COUNT);
    rundown_protection rundown;
    atomic_bool start_flag(false);

    for (auto& thr : threads) {
        thr = new std::thread([&]() {
            while (!start_flag) sched_yield();

            for (int i = 0; i < ACQUIRE_COUNT / THREAD_COUNT; ++i) {
                bool need_release;
                rundown.try_acquire(&need_release);
            }
        });
    }

    timer tmr;
    start_flag.store(true);

    for (auto& thr : threads) {
        thr->join();
    }

    const double elapsed = tmr.elapsed();
    printf("(%d threads) try_acquire() %d times: %.3lf sec (%.2lf ns/request)\n",
           THREAD_COUNT, ACQUIRE_COUNT, elapsed, elapsed * 1000 * 1000 * 1000 / ACQUIRE_COUNT);
}



BEGIN_TESTS_DECLARATION(test_rundown_protection)
DECLARE_TEST(test_basic_1)
DECLARE_TEST(test_basic_2)
DECLARE_TEST(test_basic_3)
DECLARE_TEST(test_basic_4)
DECLARE_TEST(test_multithread_1)
DECLARE_TEST(test_multithread_2)
DECLARE_TEST(test_multithread_3)
DECLARE_TEST(test_acquire_speed)
DECLARE_TEST(test_acquire_speed_mt)
END_TESTS_DECLARATION
