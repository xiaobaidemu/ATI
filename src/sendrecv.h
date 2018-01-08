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
    fd_data(socket_listener* listen, const int listenfd)
        : type(FDTYPE_SOCKET_CONNECTION), fd(listenfd), owner(listen) { }
};


struct event_data
{
    enum event_owner_type
    {
        EVENTOWNER_ENVIRONMENT,
        EVENTOWNER_CONNECTION,
        EVENTOWNER_LISTENER,
    };

    enum event_type
    {
        EVENTTYPE_UNKNOWN = 0,
        EVENTTYPE_ENVIRONMENT_DISPOSE,
        EVENTTYPE_CONNECTION_CLOSE,
        EVENTTYPE_CONNECTION_ASYNC_SEND,
        EVENTTYPE_LISTENER_CLOSE,
    };

    event_type type;
    void* owner;
    event_owner_type owner_type;

    static event_data ctor_environment_dispose(environment* env) { return event_data{ EVENTTYPE_ENVIRONMENT_DISPOSE, env, EVENTOWNER_ENVIRONMENT }; }
    static event_data ctor_connection_close(connection* conn) { return event_data{ EVENTTYPE_CONNECTION_CLOSE, conn, EVENTOWNER_CONNECTION }; }
    static event_data ctor_connection_async_send(connection* conn) { return event_data{ EVENTTYPE_CONNECTION_ASYNC_SEND, conn, EVENTOWNER_CONNECTION }; }
    static event_data ctor_listener_close(listener* listen) { return event_data{ EVENTTYPE_LISTENER_CLOSE, listen, EVENTOWNER_LISTENER }; }
};


#include "environment.h"
#include "connection.h"
#include "listener.h"
