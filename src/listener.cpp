#include "sendrecv.h"
#include "net.h"
#include <climits>
#include <sys/epoll.h>

listener::listener(environment* env) 
    : _environment(env)
{
    ASSERT(env);
}

socket_listener::socket_listener(socket_environment* env, const char* bind_ip, const uint16_t port)
    : listener(env), 
      _bind_endpoint(bind_ip, port)
{
    init();
}

socket_listener::socket_listener(socket_environment* env, const char* socket_file)
    : listener(env),
      _bind_endpoint(socket_file)
{
    init();
}

void socket_listener::init()
{
    _listen_fd = CCALL(socket(_bind_endpoint.family(), SOCK_STREAM, IPPROTO_TCP));
    MAKE_NONBLOCK(_listen_fd);
    _listen_fddata = fd_data(this, _listen_fd);

    _start_accept_required.store(false);

    // Enable reuse address
    const int enable = 1;
    CCALL(setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));    

    // Bind to the address
    CCALL(bind(_listen_fd, _bind_endpoint.data(), _bind_endpoint.socklen()));

    // Register rundown protection callback
    _rundown.register_callback([&]() {

        // Invoke OnClose callback
        if (OnClose) {
            OnClose(this);
        }

        // Close listen socket fd
        ASSERT_RESULT(_listen_fd);
        CCALL(close(_listen_fd));
        _listen_fd = INVALID_FD;

        _close_finished = true;
    });
}

void socket_listener::process_epoll_listen_fd(const uint32_t events)
{
    if (!_rundown.try_acquire()) {
        return;
    }

    endpoint remote_ep;
    socklen_t len = sizeof(remote_ep);
    const int connfd = accept(_listen_fd, remote_ep.data(), &len);
    if (connfd < 0) {
        const int error = errno;
        if (error != EAGAIN && error != EWOULDBLOCK) {
            if (OnAcceptError) {
                OnAcceptError(this, error);
            }
        }
    }
    else {
        socket_connection* conn = new socket_connection((socket_environment*)_environment, connfd, remote_ep);
        ASSERT(OnAccept);
        OnAccept(this, conn);
    }

    _rundown.release();
}

void socket_listener::process_notification(const event_data::event_type evtype)
{
    switch (evtype) {
        case event_data::EVENTTYPE_LISTENER_CLOSE: {
            break;
        }
        default: {
            FATAL("BUG: Unknown socket_listener event_type: %d\n", (int)evtype);
            ASSERT(0);
        }
    }

    _rundown.release();
}

bool socket_listener::start_accept()
{
    // Ensure OnAccept is set. Otherwise accepted connection goes nowhere.
    ASSERT(OnAccept != nullptr);

    // start_accept() can only be called once
    bool expect = false;
    if (!_start_accept_required.compare_exchange_strong(expect, true)) {
        return false;
    }

    // Acquire a rundown protection
    if (!_rundown.try_acquire()) {
        return false;
    }

    // Start listening
    CCALL(listen(_listen_fd, INT_MAX));

    // Add to socket_environment epoll
    // NOTE: listener fd is NOT edge-triggered!
    ((socket_environment*)_environment)->epoll_add(&_listen_fddata, EPOLLIN | EPOLLHUP | EPOLLERR);    

    _rundown.release();
    return true;
}

bool socket_listener::async_close()
{
    if (!_rundown.try_acquire()) {
        return false;
    }

    if (!_rundown.shutdown()) {
        _rundown.release();
        return false;
    }

    ((socket_environment*)_environment)->push_and_trigger_notification(event_data::listener_close(this));

    return true;
}

