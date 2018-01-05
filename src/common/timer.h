#pragma once

#include <sys/time.h>

class timer
{
private:
    struct timeval _start_time;

public:
    timer() {
        reset();
    }

    double elapsed() const {
        timeval now;
        gettimeofday(&now, nullptr);
        return (double)(now.tv_sec - _start_time.tv_sec) + (double)(now.tv_usec - _start_time.tv_usec) / 1000000.0;
    }

    void reset() {
        gettimeofday(&_start_time, nullptr);
    }

};
