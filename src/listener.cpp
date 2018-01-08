#include "sendrecv.h"
#include "net.h"

listener::listener(environment* env) 
    : _environment(env)
{
    ASSERT(env);
}

void socket_listener::process_epoll_listen_fd(const uint32_t events)
{
    // TODO
}

void socket_listener::process_notification(const event_data::event_type evtype)
{
    // TODO
}
