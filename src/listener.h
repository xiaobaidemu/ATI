#pragma once


class listener
{
public:
    virtual ~listener() = default;

protected:
    explicit listener(environment* env);

protected:
    environment * _environment;
};


class socket_listener : public listener
{
    friend class socket_environment;
    friend class socket_connection;

private:
    socket_listener(socket_environment* env, const char* bind_ip, const uint16_t port);
    socket_listener(socket_environment* env, const char* socket_file);
    void process_epoll_listen_fd(const uint32_t events);
    void process_notification(const event_data::event_type evtype);

private:
    int _listen_fd;
    fd_data _listen_fddata;
};
