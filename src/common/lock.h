#pragma once

#include <atomic>
#include <functional>
#include <sched.h>


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

    void acquire_run_release(const std::function<void()> func)
    {
        acquire();
        func();
        release();
    }

    ~lock()
    {
#ifdef UNITTEST
        TEST_ASSERT(!is_locked());
#endif
    }
};
