#pragma once

class environment;
class connection;
class listener;
class socket_environment;
class socket_connection;
class socket_listener;

#include <common/common.h>

#define INVALID_FD      ((int)-1)

struct fd_data
{
    enum fd_type
    {
        FDTYPE_UNKNOWN = 0,
        FDTYPE_SOCKET_NOTIFICATION_EVENT,
        FDTYPE_SOCKET_CONNECTION,
        FDTYPE_SOCKET_LISTENER,
    };

    fd_type type;
    int fd;
    void* owner;

public:
    fd_data() : type(FDTYPE_UNKNOWN), fd(INVALID_FD), owner(nullptr) { }
    fd_data(socket_environment* env, const int eventfd) 
        : type(FDTYPE_SOCKET_NOTIFICATION_EVENT), fd(eventfd), owner(env) { }
    fd_data(socket_connection* conn, const int connfd) 
        : type(FDTYPE_SOCKET_CONNECTION), fd(connfd), owner(conn) { }
    fd_data(socket_listener* lis, const int listenfd)
        : type(FDTYPE_SOCKET_LISTENER), fd(listenfd), owner(lis) { }
};


struct event_data
{
    enum event_owner_type
    {
        EVENTOWNER_ENVIRONMENT,
        EVENTOWNER_CONNECTION,
        EVENTOWNER_LISTENER,
        EVENTOWNER_MAX,
    };

    enum event_type
    {
        EVENTTYPE_UNKNOWN = 0,
        EVENTTYPE_ENVIRONMENT_DISPOSE,
        EVENTTYPE_CONNECTION_CLOSE,
        EVENTTYPE_CONNECTION_ASYNC_SEND,
        EVENTTYPE_CONNECTION_CONNECT_FAILED,
        EVENTTYPE_LISTENER_CLOSE,
        EVENTTYPE_MAX,
    };

    event_type type;
    void* owner;
    event_owner_type owner_type;

    static event_data environment_dispose(environment* env) { return event_data{ EVENTTYPE_ENVIRONMENT_DISPOSE, env, EVENTOWNER_ENVIRONMENT }; }
    static event_data connection_close(connection* conn) { return event_data{ EVENTTYPE_CONNECTION_CLOSE, conn, EVENTOWNER_CONNECTION }; }
    static event_data connection_connect_failed(connection* conn) { return event_data{ EVENTTYPE_CONNECTION_CONNECT_FAILED, conn, EVENTOWNER_CONNECTION }; }
    static event_data connection_async_send(connection* conn) { return event_data{ EVENTTYPE_CONNECTION_ASYNC_SEND, conn, EVENTOWNER_CONNECTION }; }
    static event_data listener_close(listener* listen) { return event_data{ EVENTTYPE_LISTENER_CLOSE, listen, EVENTOWNER_LISTENER }; }
};



class fragment
{
private:
    /*const*/ char* _buffer;
    /*const*/ size_t _length;
    size_t _offset;

public:
    fragment(const void* buffer, const size_t length)
        : _buffer((char*)const_cast<void*>(buffer)), _length(length), _offset(0)
    {
        if (length) {
            ASSERT(buffer);
        }
    }
    fragment() : _buffer(nullptr), _length(0), _offset(0) { }

    const void* curr_buffer() const { return _buffer + _offset; }
    size_t curr_length() const { return _length - _offset; }

    const void* original_buffer() const { return _buffer; }
    size_t original_length() const { return _length; }

    void forward(size_t n)
    {
        ASSERT(n <= curr_length());
        _offset += n;
    }
};


#include "environment.h"
#include "connection.h"
#include "listener.h"
