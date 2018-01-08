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
public:
    socket_environment();
    ~socket_environment() override;
    void dispose() override;

private:
    void process_epoll_env_notification_event_fd(const uint32_t events);
    void process_notification(const event_data::event_type evtype);
    void main_loop();
    void push_and_trigger_notification(const event_data& notification);

private:
    std::atomic_bool _dispose_required;
    std::thread* _loop_thread = nullptr;
    int _epoll_fd = INVALID_FD;

    int _notification_event_fd = INVALID_FD;
    fd_data _notification_event_fddata;    
    tsqueue<event_data> _notification_queue;
};
