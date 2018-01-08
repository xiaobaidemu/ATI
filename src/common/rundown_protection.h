#pragma once

#include <type_traits>
#include <functional>
#include <atomic>
#include <climits>

class rundown_protection
{
public:
    typedef std::function<void()> callback_t;
    typedef uint_fast64_t counter_t;
    static_assert(std::is_unsigned<counter_t>::value, "counter_t must be unsigned type.");

private:
    static const counter_t MAX_MASK = (counter_t)1 << (CHAR_BIT * sizeof(counter_t) - 1);
    static const counter_t SHUTDOWN_MASK     = MAX_MASK >> 0;
    static const counter_t CALLBACK_MASK     = MAX_MASK >> 1;
    static const counter_t ACTIVE_COUNT_MASK = ((counter_t)-1) >> 2;

    std::atomic<counter_t> _active_count;
    callback_t _callback;

public:
    rundown_protection()
    {
        _callback = nullptr;
        _active_count.store(0);
    }

    void register_callback(const callback_t& callback)
    {
        _callback = callback;
    }

    bool shutdown_required() const
    {
        return (bool)(_active_count.load() & SHUTDOWN_MASK);
    }

    bool shutdown()
    {
        const counter_t original = _active_count.fetch_or(SHUTDOWN_MASK);

        if (original == 0) {
            // !(original & SHUTDOWN_MASK) && !(original & CALLBACK_MASK) && ((original & ACTIVE_COUNT_MASK) == 0)
            invoke_callback_if_first_time();
        }

        const bool success = !(original & SHUTDOWN_MASK);
        return success;
    }

    bool try_acquire()
    {
        // At most times, we expect try_acquire() will succeed.
        // Yet, we may use a unstrict shutdown check (do not involve `lock xchg`)
        // but this check is just a waste of time!

        //const counter_t tmp = _active_count.load(std::memory_order_relaxed);
        //if (tmp & SHUTDOWN_MASK) {
        //    return false;
        //}

        const counter_t result = ++_active_count;

        if (result & (CALLBACK_MASK | SHUTDOWN_MASK)) {

            // If we have not called the callback, we must call release() to decrease _active_count
            // However, if we have ever called the callback, we do not care about _active_count's value any more!
            // This most likely saves an atomic exhange after calling shutdown()
            if (!(result & CALLBACK_MASK)) {
                release();
            }
            return false;
        }

        return true;
    }

    void release()
    {
        const counter_t remain = --_active_count;

        if (remain == SHUTDOWN_MASK) {
            // (remain & SHUTDOWN_MASK) && !(remain & CALLBACK_MASK) && ((remain & ACTIVE_COUNT_MASK) == 0)
            invoke_callback_if_first_time();
        }
    }

private:
    void invoke_callback_if_first_time()
    {
        const counter_t original = _active_count.fetch_or(CALLBACK_MASK);

#ifdef UNITTEST
        TEST_ASSERT(original & SHUTDOWN_MASK);
#endif

        if (!(original & CALLBACK_MASK)) {
            const callback_t cb = _callback;
            if (cb) {
                cb();
            }
        }
    }
};
