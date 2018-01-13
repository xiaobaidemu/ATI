#pragma once

enum connection_status
{
    CONNECTION_UNKNOWN = 0,
    CONNECTION_NOT_CONNECTED,
    CONNECTION_CONNECTING,
    CONNECTION_CONNECTED,
    CONNECTION_CONNECT_FAILED,
};

class connection
{
public:
    connection(const connection&) = delete;
    connection(connection&&) = delete;
    virtual ~connection() = default;

    std::function<void (connection* conn)> OnConnect = nullptr;
    std::function<void (connection* conn, const int error)> OnConnectError = nullptr;
    std::function<void (connection* conn, void* buffer, const size_t length)> OnSend = nullptr;
    std::function<void (connection* conn, void* buffer, const size_t length, const size_t sent_length, const int error)> OnSendError = nullptr;
    std::function<void (connection* conn, void* buffer, const size_t length)> OnReceive = nullptr;
    std::function<void (connection* conn, const int error)> OnHup = nullptr;
    std::function<void (connection* conn)> OnClose = nullptr;

    virtual bool async_send(const void* buffer, const size_t length) = 0;
    virtual bool async_close() = 0;
    virtual bool async_connect() = 0;
    virtual bool start_receive() = 0;

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
    socket_connection(socket_environment* env, const int connfd, const endpoint& remote_ep);
    void init(const bool isAccepted);
    void process_epoll_conn_fd(const uint32_t events);
    void process_notification(const event_data::event_type evtype);
    void do_send();
    void do_receive();
    void trigger_rundown_release();

public:
    bool async_close() override;
    bool async_send(const void* buffer, const size_t length) override;
    bool async_connect() override;
    bool start_receive() override;

private:
    volatile bool _close_finished = false;
    rundown_protection _rundown;
    std::atomic<connection_status> _status;

    int _conn_fd = INVALID_FD;
    int _immediate_connect_error = 0;
    fd_data _conn_fddata;

    endpoint _remote_endpoint, _local_endpoint;

    tsqueue<fragment> _sending_queue;
};
