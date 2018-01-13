#pragma once

#include "lock.h"
#include <queue>

template<typename T>
class tsqueue
{
public:
    size_t push(const T& item)
    {
        size_t new_size;
        _lock.acquire_run_release([&] {
            _queue.push(item);
            new_size = _queue.size();
        });
        return new_size;
    }

    bool try_pop(T* item)
    {
        bool success = false;
        _lock.acquire_run_release([&]() {
            if (!_queue.empty()) {
                *item = _queue.front();
                _queue.pop();
                success = true;
            }
        });
        return success;
    }

    void pop()
    {
        _lock.acquire_run_release([&]() {
            ASSERT(!_queue.empty());
            _queue.pop();
        });
    }

    bool try_front(T** item)
    {
        bool success = false;
        _lock.acquire_run_release([&]() {
            if (!_queue.empty()) {
                *item = &_queue.front();
                success = true;
            }
        });
        return success;
    }

    size_t size()
    {
        return _lock.acquire_run_release([&] {
            return _queue.size();
        });
    }

private:
    std::queue<T> _queue;
    lock _lock;
};
