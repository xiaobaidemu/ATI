#pragma once

#include "lock.h"
#include <map>

template <typename K, typename T>
class safemap
{
public:
    void Set(const K & key, const T& value) {
        _lock.acquire_run_release([&] {
            _map[key] = value;
        });
    }

    bool Get(const K & key, T *value) {
        bool success = false;
        _lock.acquire_run_release([&]{
            if(_map.find(key) != _map.end()){
                *value = _map[key];
                success = true;
            }
        });
        return success;
    }

    bool InContains(const K& key) {
        bool success = false;
        _lock.acquire_run_release([&]{
            if(_map.find(key) != _map.end()){
                success = true;
            }
        });
        return success;
    }

private:
    std::map<K, T> _map;
    lock _lock;
};