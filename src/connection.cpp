#include "sendrecv.h"
#include "net.h"

connection::connection(environment* env) 
    : _environment(env)
{
    ASSERT(env);
}

void socket_connection::process_epoll_conn_fd(const uint32_t events)
{
    // TODO
}

void socket_connection::process_notification(const event_data::event_type evtype)
{
    // TODO
}
