#pragma once

#include <type_traits>
#include <atomic>
#include <sched.h>
#include <new>
#include <vector>

#undef NDEBUG
#undef _NDEBUG
#include <cassert>


template<typename T>
struct has_on_recycle {
    template<typename C> static std::true_type dummy(decltype(&C::on_recycle));
    template<typename C> static std::false_type dummy(...);
    typedef decltype(dummy<T>(nullptr)) result_type;
};

template<typename T, typename RT>
struct call_on_recycle;

template<typename T>
struct call_on_recycle<T, std::true_type> {
    static void call(T* item) { item->on_recycle(); }
};

template<typename T>
struct call_on_recycle<T, std::false_type> {
    static void call(T*) { /*do nothing.*/ }
};

template<typename T>
void call_on_recycle_if_exist(T* item) {
    call_on_recycle<T, typename has_on_recycle<T>::result_type>::call(item);
}



template<typename T>
struct has_on_reuse {
    template<typename C> static std::true_type dummy(decltype(&C::on_reuse));
    template<typename C> static std::false_type dummy(...);
    typedef decltype(dummy<T>(nullptr)) result_type;
};

template<typename T, typename RT>
struct call_on_reuse;

template<typename T>
struct call_on_reuse<T, std::true_type> {
    static void call(T* item) { item->on_reuse(); }
};

template<typename T>
struct call_on_reuse<T, std::false_type> {
    static void call(T*) { /*do nothing.*/ }
};

template<typename T>
void call_on_reuse_if_exist(T* item) {
    call_on_reuse<T, typename has_on_reuse<T>::result_type>::call(item);
}



template<typename T>
class pool
{
    static_assert(sizeof(T) >= sizeof(void*), "pool<T> requires sizeof(T) >= sizeof(void*)");

private:
    volatile T* _head;
    std::atomic_bool _lock;

public:
    pool(const pool&) = delete;
    pool(pool&&) = delete;

    pool()
            : _head(nullptr), _lock(false)
    { }

    ~pool()
    {
        while (_head) {
            T* next = *(T**)_head;
            delete (T*)_head;
            _head = next;
        }
    }

    void push(T* item)
    {
        call_on_recycle_if_exist(item);

        while (true) {
            while (_lock.load()) {
                sched_yield();
            }
            bool expect = false;
            if (_lock.compare_exchange_strong(expect, true)) break;
        }

        *(T**)item = (T*)_head;
        _head = item;

        _lock.store(false);
    }

    T* pop()
    {
        while (true) {
            while (_lock.load()) {
                sched_yield();
            }
            bool expect = false;
            if (_lock.compare_exchange_strong(expect, true)) break;
        }

        T* result = (T*)_head;
        if (result == nullptr) {
            result = new (std::nothrow) T();
            assert(result);
        }
        else {
            _head = *(T**)result;
        }

        _lock.store(false);

        call_on_reuse_if_exist(result);
        return result;
    }
};

template<typename T>
class arraypool {
    static_assert(sizeof(T) >= sizeof(uint32_t) , "pool<T> requires sizeof(T) >= sizeof(uint32_t)");
private:
    std::vector<T> array;
    uint32_t head;
public:
    arraypool(const arraypool&) = delete;
    arraypool(arraypool&&) = delete; 
    arraypool() = delete;

    T* get(uint32_t index){
        return &array[index];
    }

    arraypool(int size){
        array.resize(size);
        for(int i = 0;i < size-1;i++){
            *((uint32_t*)(&array[i])) = i+1;
        }
        *((uint32_t*)(&array[size-1])) = 0xffffffff;
        head  = 0;
    }
    /*arraypool(){
        head = 0xffffffff;
    };*/

    uint32_t pop(){
        if(head == 0xffffffff){
            int array_size = array.size();
            T one;
            array.push_back(one);
            return array_size;
        }
        int index = head;
        head = *((uint32_t*)(&array[head]));
        memset(&array[index], 0, sizeof(T));
        return index;
    }

    void push(uint32_t index){
        memset(&array[index], 0, sizeof(T));
        *((uint32_t*)(&array[index])) = head;
        head = index;
    }
};




