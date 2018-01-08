#pragma once

#include "lock.h"
#include <queue>

template<typename T>
class tsqueue
{
public:
    void push(const T& item)
    {
        _lock.acquire_run_release([&] {
            _queue.push(item);
        });
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
