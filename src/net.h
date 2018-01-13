#pragma once

#include <common/common.h>
#include <fcntl.h>
#include <sys/socket.h>

#define MAKE_NONBLOCK(fd) \
    (([](const int __fd) -> int { \
        ASSERT_RESULT(__fd); \
        const int __flags = CCALL(fcntl(__fd, F_GETFL, 0)); \
        CCALL(fcntl(__fd, F_SETFL, __flags | O_NONBLOCK)); \
        return __fd; \
    })(fd))

#define GET_SOCKET_ERROR(sockfd) \
    (([](const int __sockfd) -> int { \
        ASSERT_RESULT(__sockfd); \
        int __error; \
        socklen_t __error_len = sizeof(__error); \
        if (getsockopt(__sockfd, SOL_SOCKET, SO_ERROR, &__error, &__error_len) < 0) { \
            __error = errno; \
            WARN("getsockopt(%d, SO_ERROR) failed with %d (%s)\n", __sockfd, __error, strerror(__error)); \
        } \
        return __error; \
    })(sockfd))

