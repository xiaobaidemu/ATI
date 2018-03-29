#include "sendrecv.h"
#include "net.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>

environment::environment()
{
}

socket_environment::socket_environment()
{
    _dispose_required.store(false);

    _epoll_fd = CCALL(epoll_create1(EPOLL_CLOEXEC));

    // We will use edge-trigger mode, don't specify EFD_SEMAPHORE
    _notification_event_fd = CCALL(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    _notification_event_fddata = fd_data(this, _notification_event_fd);

    // add _notification_event_fd to epoll
    // NOTE: EPOLLOUT is not interesting, as eventfd is always writable (before 0xFF...FE)
    epoll_add(&_notification_event_fddata, EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLET);

    // Start a thread to run main_loop
    _loop_thread = new std::thread([this]() {
        main_loop();

        // Do some cleaning work
        CCALL(close(_notification_event_fd));
        _notification_event_fd = INVALID_FD;

        CCALL(close(_epoll_fd));
        _epoll_fd = INVALID_FD;
    });
}

socket_environment::~socket_environment()
{
}

void socket_environment::main_loop()
{
    const int EVENT_BUFFER_COUNT = 256;
    epoll_event* events_buffer = new epoll_event[EVENT_BUFFER_COUNT];
    while (true) {
        const int readyCnt = epoll_wait(_epoll_fd, events_buffer, EVENT_BUFFER_COUNT, /*infinity*/-1);
        if (readyCnt < 0) {
            const int error = errno;
            if (error == EINTR) {
                // interrupted by signal, or timed-out
                continue;
            }

            ERROR("[socket_environment] epoll_wait failed with %d (%s)\n", error, strerror(error));
            break;
        }

        for (int i = 0; i < readyCnt; ++i) {
            const uint32_t curr_events = events_buffer[i].events;
            const fd_data* curr_fddata = (fd_data*)events_buffer[i].data.ptr;
            switch (curr_fddata->type) {
                case fd_data::FDTYPE_SOCKET_NOTIFICATION_EVENT: {
                    TRACE("trigger environment(eventfd=%d) events=%s\n", curr_fddata->fd, epoll_events_to_string(curr_events).c_str());

                    ASSERT(this == curr_fddata->owner);
                    ASSERT(this->_notification_event_fd == curr_fddata->fd);
                    this->process_epoll_env_notification_event_fd(curr_events);
                    break;
                }
                case fd_data::FDTYPE_SOCKET_CONNECTION: {
                    TRACE("trigger connection(fd=%d) events=%s\n", curr_fddata->fd, epoll_events_to_string(curr_events).c_str());

                    socket_connection* conn = (socket_connection*)curr_fddata->owner;
                    ASSERT(conn->_close_finished || conn->_conn_fd == curr_fddata->fd);
                    conn->process_epoll_conn_fd(curr_events);
                    break;
                }
                case fd_data::FDTYPE_SOCKET_LISTENER: {
                    TRACE("trigger listener(fd=%d) events=%s\n", curr_fddata->fd, epoll_events_to_string(curr_events).c_str());

                    socket_listener* lis = (socket_listener*)curr_fddata->owner;
                    ASSERT(lis->_close_finished || lis->_listen_fd == curr_fddata->fd);
                    lis->process_epoll_listen_fd(curr_events);
                    break;
                }
                default: {
                    FATAL("BUG: Unknown fd_type: %d\n", (int)curr_fddata->type);
                    ASSERT(0);
                    break;
                }
            }
        }

        // Check whether we should exit the loop
        if (_dispose_required) {
            break;
        }
    }
    delete[] events_buffer;
}

void socket_environment::process_epoll_env_notification_event_fd(const uint32_t events)
{
    // Consume eventfd & _notification_count
    uint64_t dummy;
    ASSERT(events & EPOLLIN);
    CCALL(read(_notification_event_fd, &dummy, sizeof(dummy)));
    
    // Process all currently queued event_data
    event_data evdata;
    while (_notification_queue.try_pop(&evdata)) {
        switch (evdata.owner_type) {
            case event_data::EVENTOWNER_ENVIRONMENT: {
                ASSERT(evdata.owner == this);
                this->process_notification(evdata.type);
                break;
            }
            case event_data::EVENTOWNER_CONNECTION: {
                socket_connection* conn = (socket_connection*)evdata.owner;
                conn->process_notification(evdata.type);
                break;
            }
            case event_data::EVENTOWNER_LISTENER: {
                socket_listener* lis = (socket_listener*)evdata.owner;
                lis->process_notification(evdata.type);
                break;
            }
            default: {
                FATAL("BUG: Unknown event_owner_type: %d\n", (int)evdata.owner_type);
                ASSERT(0);
                break;
            }
        }
    }
}

void socket_environment::push_and_trigger_notification(const event_data& notification)
{
    // Enqueue notification
    ASSERT(notification.type < event_data::EVENTTYPE_MAX);
    ASSERT(notification.owner_type < event_data::EVENTOWNER_MAX);
    const size_t new_size = _notification_queue.push(notification);

    // Write to event_fd (if necessary) to trigger epoll only if this is the only notification in queue
    ASSERT_RESULT(_notification_event_fd);
    if (new_size == 1) {
        uint64_t value = 1;
        CCALL(write(_notification_event_fd, &value, sizeof(value)));
    }
    else {
        DEBUG("skip write(_notification_event_fd): queued notification count = %lld\n", (long long)new_size);
    }
}


void socket_environment::process_notification(const event_data::event_type evtype)
{
    switch (evtype) {
        case event_data::EVENTTYPE_ENVIRONMENT_DISPOSE: {
            // Set _dispose_required to true
            _dispose_required.store(true);
            break;
        }
        default: {
            FATAL("BUG: Unknown socket_environment event_type: %d\n", (int)evtype);
            ASSERT(0);
            break;
        }
    }
}

void socket_environment::epoll_add(fd_data* fddata, const uint32_t events) const
{
    ASSERT(fddata);
    ASSERT_RESULT(fddata->fd);

    epoll_event event;
    event.events = events;
    event.data.ptr = fddata;
    CCALL(epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fddata->fd, &event));
}

void socket_environment::epoll_modify(fd_data* fddata, const uint32_t events) const
{
    ASSERT(fddata);
    ASSERT_RESULT(fddata->fd);

    epoll_event event;
    event.events = events;
    event.data.ptr = fddata;
    CCALL(epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fddata->fd, &event));
}


void socket_environment::dispose()
{
    // TODO: Use rundown_protection too?

    // Send a notification to current environment
    push_and_trigger_notification(event_data::environment_dispose(this));

    // Wait for main_loop to end
    _loop_thread->join();
}

socket_listener* socket_environment::create_listener(const char* bind_ip, const uint16_t port)
{
    return new socket_listener(this, bind_ip, port);
}

socket_listener* socket_environment::create_listener(const char* socket_file)
{
    return new socket_listener(this, socket_file);
}

socket_connection* socket_environment::create_connection(const char* connect_ip, const uint16_t port)
{
    return new socket_connection(this, connect_ip, port);
}

socket_connection* socket_environment::create_connection(const char* socket_file)
{
    return new socket_connection(this, socket_file);
}


std::string socket_environment::epoll_events_to_string(const uint32_t events)
{
    bool first = true;
    std::string result;

#define __CHECK_EPOLL_FLAG(__flag) \
    if (events & (__flag)) { \
        if (first) { \
            result += #__flag; \
            first = false; \
        } \
        else { \
            result += "|" #__flag; \
        } \
    }

    __CHECK_EPOLL_FLAG(EPOLLIN);
    __CHECK_EPOLL_FLAG(EPOLLPRI);
    __CHECK_EPOLL_FLAG(EPOLLOUT);
    __CHECK_EPOLL_FLAG(EPOLLRDNORM);
    __CHECK_EPOLL_FLAG(EPOLLRDBAND);
    __CHECK_EPOLL_FLAG(EPOLLWRNORM);
    __CHECK_EPOLL_FLAG(EPOLLWRBAND);
    __CHECK_EPOLL_FLAG(EPOLLMSG);
    __CHECK_EPOLL_FLAG(EPOLLERR);
    __CHECK_EPOLL_FLAG(EPOLLHUP);
    __CHECK_EPOLL_FLAG(EPOLLRDHUP);
    //__CHECK_EPOLL_FLAG(EPOLLWAKEUP);
    __CHECK_EPOLL_FLAG(EPOLLONESHOT);
    __CHECK_EPOLL_FLAG(EPOLLET);

#undef __CHECK_EPOLL_FLAG
    return result;
}
