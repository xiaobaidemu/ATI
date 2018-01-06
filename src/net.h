#pragma once

#include <common/common.h>
#include <fcntl.h>
#include <sys/socket.h>

#define MAKE_NONBLOCK(fd) \
    (([](const int __fd) { \
        ASSERT_RESULT(__fd); \
        const int __flags = CCALL(fcntl(__fd, F_GETFL, 0)); \
        CCALL(fcntl(__fd, F_SETFL, __flags | O_NONBLOCK)); \
        return __fd; \
    })(fd))

#define GET_SOCKET_ERROR(sockfd) \
    (([](const int __sockfd) { \
        ASSERT_RESULT(__sockfd); \
        int __error; \
        CCALL(getsockopt(__sockfd, SOL_SOCKET, SO_ERROR, &__error, sizeof(__error)); \
        return __error; \
    })(sockfd))
