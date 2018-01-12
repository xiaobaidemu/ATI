#pragma once
#include <thread>

class environment
{
public:
    virtual ~environment() = default;

protected:
    environment();
    virtual void dispose() = 0;
};

class socket_environment : public environment
{
    friend class socket_listener;
    friend class socket_connection;

public:
    socket_environment();
    ~socket_environment() override;
    void dispose() override;
    socket_listener* create_listener(const char* bind_ip, const uint16_t port);
    socket_listener* create_listener(const char* socket_file);
    socket_connection* create_connection(const char* connect_ip, const uint16_t port);
    socket_connection* create_connection(const char* socket_file);

private:
    void process_epoll_env_notification_event_fd(const uint32_t events);
    void process_notification(const event_data::event_type evtype);
    void epoll_add(fd_data* fddata, const uint32_t events) const;
    void epoll_modify(fd_data* fddata, const uint32_t events) const;
    void main_loop();
    void push_and_trigger_notification(const event_data& notification);
    static std::string epoll_events_to_string(const uint32_t events);

private:
    std::atomic_bool _dispose_required;
    std::thread* _loop_thread = nullptr;
    int _epoll_fd = INVALID_FD;

    std::atomic_int _notification_count;
    int _notification_event_fd = INVALID_FD;
    fd_data _notification_event_fddata;    
    tsqueue<event_data> _notification_queue;
};
