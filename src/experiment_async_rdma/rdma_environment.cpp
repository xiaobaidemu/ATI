#include "rdma_header.h"
#include <net.h>
#include <sys/epoll.h>
#define CQ_SIZE 100

rdma_environment::rdma_environment()
{
    _dispose_required.store(false);
    env_ec = rdma_create_event_channel();
    _efd_rdma_fd = CCALL(epoll_create1(EPOLL_CLOEXEC));
    _notification_event_rdma_fd = CCALL(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    _notification_event_rdma_fddata = rdma_fd_data(this, _notification_event_rdma_fd);
    
    MAKE_NONBLOCK(env_ec->fd);
    _rdma_channel_fddata = rdma_fd_data(this, env_ec->fd, true);
    epoll_add(&_rdma_channel_fddata, EPOLLIN|EPOLLET);
    epoll_add(&_notification_event_rdma_fddata, EPOLLIN | EPOLLET);
    
    _loop_thread = new std::thread([this](){
        main_loop();
        CCALL(close(_notification_event_rdma_fd));
        _notification_event_rdma_fd = INVALID_FD;
        CCALL(close(_efd_rdma_fd));
        _efd_rdma_fd = INVALID_FD;
        rdma_destroy_event_channel(env_ec);
        DEBUG("main_loop have already closed.\n");
    });

}
/*void rdma_environment::sighandler(int)
{
    printf("xxxxxxxxxxxxxxxxxx %d\n", pthread_self());
}*/

rdma_environment::~rdma_environment()
{
}

void rdma_environment::epoll_add(rdma_fd_data* fddata, const uint32_t events) const
{
    ASSERT(fddata); ASSERT_RESULT(fddata->fd);
    epoll_event event; event.events = events; event.data.ptr = fddata;
    CCALL(epoll_ctl(_efd_rdma_fd, EPOLL_CTL_ADD, fddata->fd, &event));
}

void rdma_environment::push_and_trigger_notification(const rdma_event_data& notification)
{
    const size_t new_size = _notification_rdma_queue.push(notification);
    TRACE("push a rdma_event_data into the _notification_rdma_queue(size:%ld).\n", new_size);
    ASSERT_RESULT(_notification_event_rdma_fd);
    if(new_size == 1){
        uint64_t value = 1;
        CCALL(write(_notification_event_rdma_fd, &value, sizeof(value)));
    }
    else{
        DEBUG("skip write(_notification_event_rdma_fd): queued notification count = %lld\n",(long long)new_size);    
    }
}

void rdma_environment::dispose()
{
    // for all_debug
    DEBUG("environment begin close.\n");
    push_and_trigger_notification(rdma_event_data::rdma_environment_dispose(this));
    _loop_thread->join();
    DEBUG("environment is closeing.\n");
}

void rdma_environment::build_params(struct rdma_conn_param *params)
{
    memset(params, 0, sizeof(*params));
    params->retry_count = 10;
    params->rnr_retry_count = 10;
}

void rdma_environment::process_rdma_channel(const uint32_t events)
{
    ASSERT(events & EPOLLIN);
    struct rdma_cm_event *event = nullptr;
    struct rdma_conn_param cm_params;
    build_params(&cm_params);
    while(rdma_get_cm_event(env_ec, &event) == 0){
        struct rdma_cm_event event_copy;
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);

        switch(event_copy.event){
            case RDMA_CM_EVENT_ADDR_RESOLVED:{
                rdma_connection *conn = (rdma_connection*)(((rdma_fd_data*)(event_copy.id->context))->owner);
                conn->build_conn_res();
                CCALL(rdma_resolve_route(event_copy.id, TIMEOUT_IN_MS));
                break;                               
            }
            case RDMA_CM_EVENT_ROUTE_RESOLVED:{
                CCALL(rdma_connect(event_copy.id, &cm_params));
                TRACE("Finish rdma_cm_event_rout_resolved event.\n");
                break;                        
            }
            case RDMA_CM_EVENT_CONNECT_REQUEST:{
                rdma_connection *new_conn = create_rdma_connection_passive(event_copy.id, event_copy.listen_id);
                //if(((rdma_fd_data*)(event_copy.listen_id->context))->owner) printf("------------------\n");
                //event_copy.id->context = new_conn; 
                if(map_id_conn.find((intptr_t)(event_copy.id)) == map_id_conn.end()){
                    map_id_conn[(intptr_t)(event_copy.id)] = new_conn;
                }
                else{
                    FATAL("Cannot fix bug when handle rdma_cm_event_connect_request.\n");
                    ASSERT(0);
                }
                //记录id和
                CCALL(rdma_accept(event_copy.id, &cm_params));
                TRACE("Listener finish rdma_cm_event_connect_request event.\n");
                break;                        
            }                                   
            case RDMA_CM_EVENT_ESTABLISHED:{
                //DEBUG("handle rdma_cm_event_established event.\n");
                if(map_id_conn.find((intptr_t)(event_copy.id)) == map_id_conn.end()){
                    rdma_connection *conn = (rdma_connection*)(((rdma_fd_data*)(event_copy.id->context))->owner);
                    conn->process_established();
                    NOTICE("%s to %s active connection established.\n",
                            conn->local_endpoint().to_string().c_str(), 
                            conn->remote_endpoint().to_string().c_str());
                }
                else{
                    rdma_connection *new_passive_conn = map_id_conn[(intptr_t)(event_copy.id)];
                    rdma_listener *listen = new_passive_conn->conn_lis;
                    listen->process_accept_success(new_passive_conn);
                    NOTICE("%s from %s passive connection established.\n",
                        new_passive_conn->local_endpoint().to_string().c_str(),
                        new_passive_conn->remote_endpoint().to_string().c_str());
                }
                break;                        
            }
            case RDMA_CM_EVENT_DISCONNECTED:{
                DEBUG("handle rdma_cm_event_disconnected event.\n");
                rdma_connection *conn = nullptr;
                if(map_id_conn.find((intptr_t)(event_copy.id)) == map_id_conn.end()){
                    conn = (rdma_connection*)(((rdma_fd_data*)(event_copy.id->context))->owner);
                    conn->_status.store(CONNECTION_CLOSED);
                    DEBUG("Active connection is disconneting.\n");
                }
                else{
                    conn = map_id_conn[(intptr_t)(event_copy.id)];
                    conn->_status.store(CONNECTION_CLOSED);
                    map_id_conn.erase((intptr_t)(event_copy.id));
                    DEBUG("Passive connection is disconnecting.\n");
                }
                conn->close_rdma_conn();
                break; 
            }
            case RDMA_CM_EVENT_ADDR_ERROR:
            case RDMA_CM_EVENT_ROUTE_ERROR:
            case RDMA_CM_EVENT_UNREACHABLE:
            case RDMA_CM_EVENT_REJECTED:{
                if(event_copy.id && event_copy.id->context){
                    rdma_connection *conn = (rdma_connection*)(((rdma_fd_data*)(event_copy.id->context))->owner);
                    conn->process_established_error();
                } 
                break;
            }
            case RDMA_CM_EVENT_CONNECT_ERROR:{
                if(event_copy.listen_id && event_copy.listen_id->context){
                    rdma_listener* lis = (rdma_listener*)(((rdma_fd_data*)(event_copy.listen_id->context))->owner);
                    lis->process_accept_fail();
                }
                else if(event_copy.id && event_copy.id->context){
                    rdma_connection *conn = (rdma_connection*)(((rdma_fd_data*)(event_copy.id->context))->owner);
                    conn->process_established_error();
                }
                else{
                    FATAL("BUG: Cannot handle error event %s because cannot find conn/listen object.\n", rdma_event_str(event_copy.event));
                    ASSERT(0);break;
                }
                break;
            }
            default:{
                FATAL("BUG: Cannot handle event:%s\n",rdma_event_str(event_copy.event));
                ASSERT(0);
                break;
            }
        }
    }
    return;
}

void rdma_environment::main_loop()
{
    DEBUG("ENVIRONMENT start main_loop.\n");
    const int EVENT_BUFFER_COUNT = 256;
    epoll_event* events_buffer = new epoll_event[EVENT_BUFFER_COUNT];
    struct ibv_wc ret_wc_array[CQE_MIN_NUM];
    while(true){
       // DEBUG("---------------------------------------------\n");
        const int readyCnt = epoll_wait(_efd_rdma_fd, events_buffer, EVENT_BUFFER_COUNT, -1);
        if(readyCnt<0){
            const int error = errno;
            if(error == EINTR) continue;
            ERROR("[rdma_environment] epoll_wait failed with %d (%s)\n", 
                error, strerror(error));
            break;
        }
        for(int i = 0;i < readyCnt;++i){
            const uint32_t curr_events = events_buffer[i].events;
            const rdma_fd_data* curr_rdmadata = (rdma_fd_data*)events_buffer[i].data.ptr;
            switch(curr_rdmadata->type){
                case rdma_fd_data::RDMATYPE_CHANNEL_EVENT:{
                    TRACE("trigger rdma_channel fd = %d.\n", env_ec->fd);
                    ASSERT(this == curr_rdmadata->owner);
                    ASSERT(curr_rdmadata->fd == env_ec->fd);
                    this->process_rdma_channel(curr_events);
                    break;
                }
                case rdma_fd_data::RDMATYPE_NOTIFICATION_EVENT:{
                    TRACE("trigger rdma_eventfd = %d %d\n", curr_rdmadata->fd, _notification_event_rdma_fd);
                    ASSERT(this == curr_rdmadata->owner);
                    ASSERT(curr_rdmadata->fd == _notification_event_rdma_fd);
                    this->process_epoll_env_notificaton_event_rdmafd(curr_events);
                    break;                                            
                }
                case rdma_fd_data::RDMATYPE_ID_CONNECTION:{
                    //表示当前已经有完成任务发生了，可以通过ibv_get_cq_event获取
                    struct ibv_cq *ret_cq; void *ret_ctx; struct ibv_wc wc;
                    rdma_connection *conn = (rdma_connection*)curr_rdmadata->owner;
                    CCALL(ibv_get_cq_event(conn->conn_comp_channel, &ret_cq, &ret_ctx));
                    conn->ack_num++;
                    //此处会有坑
                    if(conn->ack_num == ACK_NUM_LIMIT){ 
                        ibv_ack_cq_events(ret_cq, conn->ack_num);
                        TRACE("ibv_ack_cq_events %d\n", ACK_NUM_LIMIT);
                        conn->ack_num = 0;
                    }
                    CCALL(ibv_req_notify_cq(ret_cq, 0));
                    int num_cqe;
                    while(num_cqe = ibv_poll_cq(ret_cq, CQE_MIN_NUM, ret_wc_array)){
                        TRACE("ibv_poll_cq() get %d cqe.\n", num_cqe);
                        conn->process_poll_cq(ret_cq, ret_wc_array, num_cqe);
                    }
                    break;
                }
                default:{
                    FATAL("BUG: Unknown rdma_fd_data: %d\n", (int)curr_rdmadata->type);
                    ASSERT(0);break;
                }
            }
        }
        if(_dispose_required.load()){
            DEBUG("ready to close the main_loop.\n");
            break;
        }
    }
    delete[] events_buffer;
    return;
}

long long rdma_environment::get_curtime(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000000 + tv.tv_usec;
}

void rdma_environment::process_close_queue(){
    while(true){
        close_conn_info *conn_info;
        bool issuc = _ready_close_queue.try_front(&conn_info);
        if(!issuc) break;
        else{
            if(get_curtime() - (conn_info->time_ready_close) >= 0){
                if(conn_info->closing_conn->_status.load() == CONNECTION_CONNECTED){
                    WARN("ready to rdma_disconnection.\n");
                    conn_info->closing_conn->close_rdma_conn();
                    //rdma_destroy_qp(conn_info->closing_conn->conn_id);
                    //conn_info->closing_conn->conn_id = nullptr;
                    //rdma_disconnect(conn_info->closing_conn->conn_id);
                }
                else{
                    ASSERT(conn_info->closing_conn->_status.load() == CONNECTION_CLOSED);
                }
                _ready_close_queue.pop();
                continue;
            }
            else break;
        }
    }
    if(_ready_close_queue.size() > 0){
        push_and_trigger_notification(rdma_event_data::rdma_connection_close());
    }

}
//目前要处理的内容只有发送操作
void rdma_environment::process_epoll_env_notificaton_event_rdmafd(const uint32_t events)
{
    uint64_t dummy; ASSERT(events & EPOLLIN);
    CCALL(read(_notification_event_rdma_fd, &dummy, sizeof(dummy)));
    //处理_notification_rdma_queue中的事物
    rdma_event_data evdata;
    while(_notification_rdma_queue.try_pop(&evdata)){
        switch(evdata.type){
            case rdma_event_data::RDMA_EVENTTYPE_ASYNC_SEND:{
                rdma_connection* conn = (rdma_connection*)evdata.owner;
                conn->process_rdma_async_send();
                break;
            }
            case rdma_event_data::RDMA_EVENTTYPE_CONNECTION_CLOSE:{
                //rdma_connection* conn = (rdma_connection*)evdata.owner;
                //conn->_rundown.release();
                this->process_close_queue();
                break;
            }
            case rdma_event_data::RDMA_EVENTTYPE_FAILED_CONNECTION_CLOSE:{
                WARN("ready to close failed_connected connectioni......\n");
                rdma_connection* conn = (rdma_connection*)evdata.owner;
                conn->close_rdma_conn();
                conn->_rundown.release();
            }
            case rdma_event_data::RDMA_EVENTTYPE_LISTENER_CLOSE:{
                rdma_listener* lis = (rdma_listener*)evdata.owner;
                lis->_rundown.release();
                break;
            }
            case rdma_event_data::RDMA_EVENTTYPE_ENVIRONMENT_DISPOSE:{
                _dispose_required.store(true);
                TRACE("trigger RDMA_EVENTTYPE_ENVIRONMENT_DISPOSE .\n");
                break;
            }
            default: {
                FATAL("BUG: Unknown rdma_environment event_type: %d\n", (int)evdata.type);
                ASSERT(0);break;
            }
        }
    }
}


rdma_connection* rdma_environment::create_rdma_connection(const char* connect_ip, const uint16_t port)
{
   return new rdma_connection(this, connect_ip, port); 
}

rdma_listener* rdma_environment::create_rdma_listener(const char* bind_ip, const uint16_t port)
{
    return new rdma_listener(this, bind_ip, port);
}

rdma_connection* rdma_environment::create_rdma_connection_passive(struct rdma_cm_id *new_conn, struct rdma_cm_id *listen_id)
{
    return new rdma_connection(this, new_conn, listen_id);
}

