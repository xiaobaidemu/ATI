#pragma once


class listener
{
public:
    listener(const listener&) = delete;
    listener(listener&&) = delete;
    virtual ~listener() = default;
    virtual bool start_accept() = 0;
    virtual bool async_close() = 0;

    std::function<void(listener* lis, connection* conn)> OnAccept = nullptr;
    std::function<void(listener* lis, const int error)> OnAcceptError = nullptr;
    std::function<void(listener* lis)> OnClose = nullptr;

protected:
    explicit listener(environment* env);

protected:
    environment* _environment;
};


class socket_listener : public listener
{
    friend class socket_environment;
    friend class socket_connection;

public:
    bool start_accept() override;
    bool async_close() override;

private:
    socket_listener(socket_environment* env, const char* bind_ip, const uint16_t port);
    socket_listener(socket_environment* env, const char* socket_file);
    void init();
    void process_epoll_listen_fd(const uint32_t events);
    void process_notification(const event_data::event_type evtype);

private:
    int _listen_fd = INVALID_FD;
    fd_data _listen_fddata;

    endpoint _bind_endpoint;

    std::atomic_bool _start_accept_required;
    std::atomic_bool _close_required;

    rundown_protection _rundown;
};
