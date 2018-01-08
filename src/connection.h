#pragma once

class connection
{
public:
    virtual ~connection() = default;

protected:
    explicit connection(environment* env);

protected:
    environment* _environment;
};


class socket_connection : public connection
{
    friend class socket_environment;
    friend class socket_listener;

private:
    socket_connection(socket_environment* env, const char* connect_ip, const uint16_t port);
    socket_connection(socket_environment* env, const char* socket_file);
    socket_connection(socket_environment* env, const int connfd);
    void process_epoll_conn_fd(const uint32_t events);
    void process_notification(const event_data::event_type evtype);

private:
    int _conn_fd;
    fd_data _conn_fddata;
};