#pragma once

#include <atomic>
#include <sched.h>

#ifndef COMMON_LOCK_SPIN_RETRY
#define COMMON_LOCK_SPIN_RETRY      ((uint64_t)1000)
#endif
static_assert(COMMON_LOCK_SPIN_RETRY >= 0, "Invalid COMMON_LOCK_SPIN_RETRY value");


class lock
{
private:
    std::atomic_bool _locked;

public:
    explicit lock(const bool init_locked = false)
        : _locked(init_locked)
    { }

    bool is_locked() const
    {
        return _locked.load();
    }

    bool try_acquire()
    {
        bool expect = false;
        return _locked.compare_exchange_strong(expect, true);
    }

    void acquire()
    {
        for (uint_fast64_t retryCnt = 0; retryCnt < COMMON_LOCK_SPIN_RETRY; ++retryCnt) {
            if (try_acquire()) {
                return;
            }
        }

        while (!try_acquire()) {
            const int result = sched_yield();
#ifdef UNITTEST
            TEST_ASSERT(result == 0);
#endif
            (void)result;
        }
    }

    void release()
    {
#ifdef UNITTEST
        TEST_ASSERT(is_locked());
#endif
        _locked.store(false);
    }

    ~lock()
    {
#ifdef UNITTEST
        TEST_ASSERT(!is_locked());
#endif
    }
};
