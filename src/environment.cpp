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
    epoll_event event;
    event.events = EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLET;
    event.data.ptr = &_notification_event_fddata;
    CCALL(epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _notification_event_fd, &event));

    // Start a thread to run main_loop
    _loop_thread = new std::thread([this]() {
        main_loop();

        // TODO: Do some cleaning work
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
                    ASSERT(this == curr_fddata->owner);
                    ASSERT(this->_notification_event_fd == curr_fddata->fd);
                    this->process_epoll_env_notification_event_fd(curr_events);
                }
                case fd_data::FDTYPE_SOCKET_CONNECTION: {
                    socket_connection* conn = (socket_connection*)curr_fddata->owner;
                    ASSERT(conn->_conn_fd == curr_fddata->fd);
                    conn->process_epoll_conn_fd(curr_events);
                }
                case fd_data::FDTYPE_SOCKET_LISTENER: {
                    socket_listener* listen = (socket_listener*)curr_fddata->owner;
                    ASSERT(listen->_listen_fd == curr_fddata->fd);
                    listen->process_epoll_listen_fd(curr_events);
                }
                default: {
                    FATAL("BUG: Unknown fd_type: %d\n", (int)curr_fddata->type);
                    ASSERT(0);
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
    // Consume eventfd
    uint64_t dummy;
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
            }
        }
    }
}

void socket_environment::push_and_trigger_notification(const event_data& notification)
{
    // Enqueue notification
    _notification_queue.push(notification);

    // Write to event_fd to trigger epoll
    ASSERT_RESULT(_notification_event_fd);
    uint64_t value = 1;
    CCALL(write(_notification_event_fd, &value, sizeof(value)));
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
        }
    }
}


void socket_environment::dispose()
{
    // Send a notification to current environment
    push_and_trigger_notification(event_data::ctor_environment_dispose(this));

    // Wait for main_loop to end
    _loop_thread->join();
}
