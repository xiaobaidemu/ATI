#pragma once

#include "timer.h"
#include <cstdio>
#include <sys/syscall.h>
#include <unistd.h>

#define __LOGGER_OUTPUT(__level, __color, __format, ...) \
    do { \
        extern timer common_logger_timer; \
        extern const bool common_logger_isatty; \
        if (!common_logger_isatty) { \
            printf("%-7.3lf | %-5d | [%s] %s: " __format, \
                   common_logger_timer.elapsed(), (int)syscall(SYS_gettid), __FUNCTION__, __level, ##__VA_ARGS__); \
        } \
        else { \
            printf("\033[37;1m%-7.3lf\033[30;1m | \033[0m\033[36m%-5d\033[30;1m | \033[34;1m[%s] \033[%s;4m%s\033[0m\033[%sm: " __format "\033[0m", \
                   common_logger_timer.elapsed(), (int)syscall(SYS_gettid), __FUNCTION__, __color, __level, __color, ##__VA_ARGS__); \
        } \
        fflush(stdout); \
    } while(false)


#define TRACE(__format, ...)    __LOGGER_OUTPUT("TRACE",  "30;1", __format, ##__VA_ARGS__)
#define DEBUG(__format, ...)    __LOGGER_OUTPUT("DEBUG",  "35;1", __format, ##__VA_ARGS__)
#define INFO(__format, ...)     __LOGGER_OUTPUT("INFO",   "37",   __format, ##__VA_ARGS__)
#define NOTICE(__format, ...)   __LOGGER_OUTPUT("NOTICE", "32;1", __format, ##__VA_ARGS__)
#define WARN(__format, ...)     __LOGGER_OUTPUT("WARN",   "33;1", __format, ##__VA_ARGS__)
#define SUCC(__format, ...)     __LOGGER_OUTPUT("SUCC",   "32;1", __format, ##__VA_ARGS__)
#define ERROR(__format, ...)    __LOGGER_OUTPUT("ERROR",  "31;1", __format, ##__VA_ARGS__)
#define FATAL(__format, ...)    __LOGGER_OUTPUT("FATAL",  "31;1", __format, ##__VA_ARGS__)
#define IDEBUG(__format, ...)    __LOGGER_OUTPUT("IDEBUG",  "36;1", __format, ##__VA_ARGS__)
#define ITRACE(__format, ...)    __LOGGER_OUTPUT("ITRACE",  "34;1", __format, ##__VA_ARGS__)
#define ITR_SEND(__format, ...)  __LOGGER_OUTPUT("ISEND",  "34;1", __format, ##__VA_ARGS__)
#define ITR_RECV(__format, ...)  __LOGGER_OUTPUT("IRECV",  "36;1", __format, ##__VA_ARGS__)
#define ITR_POLL(__format, ...)    __LOGGER_OUTPUT("POLL",  "30;1", __format, ##__VA_ARGS__)
#define ITR_SPECIAL(__format, ...)    __LOGGER_OUTPUT("SPECIAL",  "36;1", __format, ##__VA_ARGS__)
