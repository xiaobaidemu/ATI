#include "sendrecv.h"
#include "net.h"
#include <sys/epoll.h>

connection::connection(environment* env) 
    : _environment(env)
{
    ASSERT(env);
    is_sent_head.store(true);
}

socket_connection::socket_connection(socket_environment* env, const char* remote_ip, const uint16_t port)
    : connection(env),
      _remote_endpoint(remote_ip, port)
{
    _conn_fd = CCALL(socket(_remote_endpoint.family(), SOCK_STREAM, IPPROTO_TCP));

    init(false);
}

socket_connection::socket_connection(socket_environment* env, const char* socket_file)
    : connection(env),
      _remote_endpoint(socket_file)
{
    _conn_fd = CCALL(socket(_remote_endpoint.family(), SOCK_STREAM, 0));

    init(false);
}

socket_connection::socket_connection(socket_environment* env, const int connfd, const endpoint& remote_ep)
    : connection(env),
      _remote_endpoint(remote_ep)
{
    ASSERT_RESULT(connfd);
    _conn_fd = connfd;

    init(true);
}

void socket_connection::init(const bool isAccepted)
{
    // Make connection fd non-blocking
    ASSERT_RESULT(_conn_fd);
    MAKE_NONBLOCK(_conn_fd);

    _conn_fddata = fd_data(this, _conn_fd);

    if (isAccepted) {
        _status.store(CONNECTION_CONNECTED);

        // Update local endpoint if this is an accepted connection
        update_local_endpoint();

        // Add to epoll if this is an accepted connection
        ((socket_environment*)_environment)->epoll_add(&_conn_fddata, EPOLLOUT | EPOLLERR | EPOLLRDHUP | EPOLLET);
    }
    else {
        _status.store(CONNECTION_NOT_CONNECTED);
    }

    // Register rundown protection callback
    _rundown.register_callback([&]() {

        const int error = ECANCELED;  // TODO: Define what error code?
        fragment frag;
        while (_sending_queue.try_pop(&frag)) {
            ASSERT(frag.curr_length() > 0);

            // Invoke OnSendError callbacks
            if (OnSendError) {
                OnSendError(this, (void*)frag.original_buffer(), frag.original_length(), frag.original_length() - frag.curr_length(), error);
            }
             
            // This was acquired in async_send(), now release.
            _rundown.release();
        }


        // Invoke OnClose callback
        if (OnClose) {
            OnClose(this);
        }

        // Close connection socket fd
        ASSERT_RESULT(_conn_fd);
        CCALL(close(_conn_fd));
        _conn_fd = INVALID_FD;

        _close_finished = true;
    });
}

void socket_connection::process_epoll_conn_fd(const uint32_t events)
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        WARN("socket_connection(fd=%d) _rundown.try_acquire() failed\n", _conn_fd);
        _rundown.release();
        return;
    }

    // Check connected first
    const connection_status status = _status;
    switch (status) {
        case CONNECTION_CONNECTING: {

            // Update local endpoint first
            update_local_endpoint();

            // Check whether connect() failed
            if ((events & EPOLLERR) || (events & EPOLLHUP) || (events & EPOLLRDHUP)) {

                // Change _status to CONNECTION_CONNECT_FAILED
                connection_status original = _status.exchange(CONNECTION_CONNECT_FAILED);
                ASSERT(original == CONNECTION_CONNECTING);

                // _immediate_connect_error is the error code if connect() failed synchronously
                // If this value is 0, we try to get error code by getsockopt(SO_ERROR)
                const int error = GET_SOCKET_ERROR(_conn_fd);
                ASSERT(error != 0); // TODO: this assertion may fail!

                if (OnConnectError) {
                    OnConnectError(this, error);
                }
            }
            else {

                // Change _status to CONNECTION_CONNECTED
                connection_status original = _status.exchange(CONNECTION_CONNECTED);
                ASSERT(original == CONNECTION_CONNECTING);

                if (OnConnect) {
                    OnConnect(this);
                }
            }

            // This was acquired in async_connect(), now release.
            _rundown.release();

            break;
        }
        case CONNECTION_CONNECTED: {

            if (events & EPOLLIN) {
                do_receive();
            }

            if ((events & EPOLLERR) || (events & EPOLLHUP) || (events & EPOLLRDHUP)) {

                if (!(events & EPOLLERR) && ((events & EPOLLHUP) || (events & EPOLLRDHUP))) {
                    // Only EPOLLRDHUP is reported. Invoke OnHup
                    if (OnHup) {
                        OnHup(this, 0);
                    }
                }
                else {
                    // TODO: How to deal with EPOLLERR?
                    FATAL("socket_connection(fd=%d): events = %d. TODO!!! (OnHup?)\n", _conn_fd, events);
                    ASSERT(0);
                }
            }
            if (events & EPOLLOUT) {
                do_send();
            }

            break;
        }
        default: {
            ERROR("BUG: Unexpected status = %d\n", (int)status);
            break;
        }
    }

    ASSERT(need_release);
    _rundown.release();
}

void socket_connection::process_notification(const event_data::event_type evtype)
{
    switch (evtype) {
        case event_data::EVENTTYPE_CONNECTION_CLOSE: {
            // We do not need to do anything

            // This was acquired in async_close()
            _rundown.release();
            break;
        }
        case event_data::EVENTTYPE_CONNECTION_RUNDOWN_RELEASE: {
            _rundown.release();
            break;
        }
        case event_data::EVENTTYPE_CONNECTION_CONNECT_FAILED: {

            // Change _status to CONNECTION_CONNECT_FAILED
            connection_status original = _status.exchange(CONNECTION_CONNECT_FAILED);
            ASSERT(original == CONNECTION_CONNECTING);

            update_local_endpoint();

            if (OnConnectError) {
                OnConnectError(this, _immediate_connect_error);
            }

            // This was acquired in async_connect()
            _rundown.release();
            break;
        }
        case event_data::EVENTTYPE_CONNECTION_ASYNC_SEND: {
            do_send();
            break;
        }
        default: {
            FATAL("BUG: Unknown socket_listener event_type: %d\n", (int)evtype);
            ASSERT(0);
        }
    }
}

void socket_connection::do_send()
{
    fragment* frag;

    while (_sending_queue.try_front(&frag)) {

        ASSERT(frag->curr_length() > 0);

        const ssize_t sentCnt = write(_conn_fd, frag->curr_buffer(), frag->curr_length());
        if (sentCnt < 0) {
            const int error = errno;
            if (error == EAGAIN || error == EWOULDBLOCK) {
                TRACE("socket_connection(fd=%d) sending buffer full. break.\n", _conn_fd);
                break;
            }

            if (OnSendError) {
                OnSendError(this, (void*)frag->original_buffer(), frag->original_length(), frag->original_length() - frag->curr_length(), error);
            }
            _sending_queue.pop();

            // This was acquired in async_send(), now release.
            _rundown.release();
        }
        else if (sentCnt > 0) {
            frag->forward((size_t)sentCnt);
            INFO("socket_connection(fd=%d) sent %lld (total %lld / required %lld)\n", 
                _conn_fd, (long long)sentCnt, (long long)frag->original_length() - frag->curr_length(), (long long)frag->original_length());

            if (frag->curr_length() == 0) {
                if (OnSend) {
                    OnSend(this, (void*)frag->original_buffer(), frag->original_length());
                }
                _sending_queue.pop();

                // This was acquired in async_send(), now release.
                _rundown.release();
            }
        }
        else {
            // sendCnt == 0
            WARN("write(connfd=%d) returns 0, errno = %d (%s)\n", _conn_fd, errno, strerror(errno));
            break;
        }
    }
}

void socket_connection::do_receive()
{
    const int BUFFER_SIZE = 1024 * 1024;  // 16MB, TODO: use pool
    void* buffer = malloc(BUFFER_SIZE);
    ASSERT(buffer != nullptr);

    while (true) {
        const ssize_t recvCnt = read(_conn_fd, buffer, BUFFER_SIZE);
        if (recvCnt > 0) {
            if (OnReceive) {
                OnReceive(this, buffer, (size_t)recvCnt);
            }
        }
        else if (recvCnt == 0) {
            // NOTE: We don't call OnHup here.
            // As EPOLLRDHUP will be reported to epoll, leave OnHup there. 

            if (OnHup) {
                OnHup(this, 0);
            }
            break;
        }
        else {
            // now: recvCnt < 0
            const int error = errno;
            if (error == EAGAIN || error == EWOULDBLOCK) {
                TRACE("socket_connection(fd=%d) all pending data received. break.\n", _conn_fd);
                break;
            }
            else {
                // TODO: What should we do to handle this situation?
                FATAL("read() failed: %d (%s). TODO: call OnHup?\n", error, strerror(error));
                ASSERT(0);
                if (OnHup) {
                    OnHup(this, error);
                }
                break;
            }
        }
    }

    free(buffer);
}

void socket_connection::update_local_endpoint()
{
    socklen_t len = _remote_endpoint.socklen();
    CCALL(getsockname(_conn_fd, _local_endpoint.data(), &len));
}

void socket_connection::trigger_rundown_release()
{
    ((socket_environment*)_environment)->push_and_trigger_notification(event_data::connection_rundown_release(this));
}

bool socket_connection::async_close()
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            trigger_rundown_release();
        }
        return false;
    }

    if (!_rundown.shutdown()) {
        trigger_rundown_release();
        return false;
    }

    ((socket_environment*)_environment)->push_and_trigger_notification(event_data::connection_close(this));

    return true;
}

bool socket_connection::async_send(const void* buffer, const size_t length)
{
    // Just don't allow send 0 bytes
    ASSERT(buffer != nullptr);
    ASSERT(length > 0);

    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            trigger_rundown_release();
        }
        return false;
    }

    const size_t new_size = _sending_queue.push(fragment(buffer, length));

    // Trigger notification only if this is the only fragment to send
    if (new_size == 1) {
        ((socket_environment*)_environment)->push_and_trigger_notification(event_data::connection_async_send(this));
    }

    return true;
}

bool socket_connection::async_send_many(const std::vector<fragment> frags)
{
    ASSERT(!frags.empty());
#ifndef NDEBUG
    for (const fragment& frag : frags) {
        ASSERT(frag.original_buffer() != nullptr);
        ASSERT(frag.original_length() != 0);
    }
#endif

    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            trigger_rundown_release();
        }
        return false;
    }

    // Increase _rundown count as if we call async_send() so many times
    for (size_t i = 0; i < frags.size() - 1; ++i) {
        bool success = _rundown.try_acquire(&need_release);
        ASSERT(success);
        ASSERT(need_release);
    }

    const size_t new_size = _sending_queue.push_many(frags);

    // Trigger notification only if these are the only fragments to send
    if (new_size == frags.size()) {
        ((socket_environment*)_environment)->push_and_trigger_notification(event_data::connection_async_send(this));
    }

    return true;
}

bool socket_connection::async_connect()
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            trigger_rundown_release();
        }
        return false;
    }

    connection_status expect = CONNECTION_NOT_CONNECTED;
    if (!_status.compare_exchange_strong(expect, CONNECTION_CONNECTING)) {
        trigger_rundown_release();
        return false;
    }

    ASSERT_RESULT(_conn_fd);

    // Try connect asynchronously
    _immediate_connect_error = 0;
    const int result = connect(_conn_fd, _remote_endpoint.data(), _remote_endpoint.socklen());
    if (result < 0) {
        const int error = errno;
        ASSERT(error != 0);
        if (error != EINPROGRESS) {
            _immediate_connect_error = error;
            ERROR("socket_connection(fd=%d) connect to %s failed: %d (%s)\n", 
                _conn_fd, _remote_endpoint.to_string().c_str(), error, strerror(error));

            // NOTE: in this situation, OnConnectError will be triggered.
            // So do not directly return false.
            // Trigger a notification instead
            ((socket_environment*)_environment)->push_and_trigger_notification(event_data::connection_connect_failed(this));
        }
    }

    // Add _conn_fd to epoll (if not failed synchronously)
    // DO NOT include EPOLLIN (modify epoll to add EPOLLIN in start_receive())
    if (_immediate_connect_error == 0) {
        ((socket_environment*)_environment)->epoll_add(&_conn_fddata, EPOLLOUT | EPOLLERR | EPOLLRDHUP | EPOLLET);
    }

    return true;
}

bool socket_connection::start_receive()
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            trigger_rundown_release();
        }
        return false;
    }

    if (_status != CONNECTION_CONNECTED) {
        trigger_rundown_release();
        return false;
    }

    // Modify _conn_fd in epoll£º add EPOLLIN
    ASSERT_RESULT(_conn_fd);
    ASSERT(_conn_fddata.fd == _conn_fd);
    ((socket_environment*)_environment)->epoll_modify(&_conn_fddata, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLRDHUP | EPOLLET);

    trigger_rundown_release();
    return true;
}
