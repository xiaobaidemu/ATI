#include <infiniband/verbs.h>
#include "rdma_header.h"

rdma_connection::rdma_connection(rdma_environment *env, const char* connect_ip, const uint16_t port)
    :connection(env), _remote_endpoint(connect_ip, port), peer_rest_wr(MAX_RECV_WR), recvinfo_pool()
{
    //创建rdma_cm_id,并注意关联id对应的类型是conn还是用于listen
    peer_start_recv.store(false);//false means that the peer recv is not ready for receive
    self_ready_closed.store(false);//false means that connection self doesn't call async_close
    peer_ready_closed.store(false);//false means that this side connection doesn't recv the ack of close
    self_ready_close.store(false);
    conn_type = rdma_fd_data(this);
    CCALL(rdma_create_id(env->env_ec, &conn_id, &conn_type, RDMA_PS_TCP));
    _status.store(CONNECTION_NOT_CONNECTED);
    int test_peer_rest_wr = peer_rest_wr.load();
    ASSERT(test_peer_rest_wr == MAX_RECV_WR);
    register_rundown();
}

rdma_connection::rdma_connection(rdma_environment* env, struct rdma_cm_id *new_conn_id, struct rdma_cm_id *listen_id)
    :connection(env), conn_id(new_conn_id), peer_rest_wr(MAX_RECV_WR)
{
    peer_start_recv.store(false);//the peer recv is not ready for receive
    self_ready_closed.store(false);//false means that connection self doesn't call async_close
    peer_ready_closed.store(false);//false means that this side connection doesn't recv the ack of close
    self_ready_close.store(false);
    int test_peer_rest_wr = peer_rest_wr.load();
    ASSERT(test_peer_rest_wr == MAX_RECV_WR);
    update_local_endpoint();
    update_remote_endpoint();
    build_conn_res();
    conn_lis = (rdma_listener*)(((rdma_fd_data*)(listen_id->context))->owner);
    register_rundown();
}

void rdma_connection::register_rundown()
{
    _rundown.register_callback([&]() {
        const int error = ECANCELED;//use ECANCELED?
        rdma_sge_list *sge_list;
        size_t has_not_send = 0;
        while (_sending_queue.try_pop(&sge_list)) {
            ASSERT(sge_list->total_length > 0);

            for(auto mr_addr:sge_list->mr_list){
                CCALL(ibv_dereg_mr(mr_addr));
            }

            for(int i  = 0;i < sge_list->num_sge;++i){
                rdma_sge_list::sge_info &element = sge_list->sge_info_list[i];
                if(element.end){
                    has_not_send += sge_list->sge_list[i].length;
                    if(OnSendError){
                        OnSendError(this, (void*)element.send_start, element.send_length,
                                    element.send_length-has_not_send, error);
                    }
                    _rundown.release();
                    has_not_send = 0;
                }
                else{
                    has_not_send += sge_list->sge_list[i].length;
                }
            }

            /*if(sge_list->end){
                has_not_send += sge_list->total_length;
                if(OnSendError){
                    OnSendError(this, (void*)sge_list->send_start, sge_list->send_length,
                                sge_list->send_length-has_not_send, error);
                }
                _rundown.release();
                has_not_send = 0;
            }
            else{
                has_not_send += sge_list->total_length;
            }*/
        }

        if (OnClose) {
            OnClose(this);
        }
        
        _close_finished = true;
        DEBUG("END ~~~ have already trigger the rundown's register_callback.\n");
    });
}

long long rdma_connection::get_curtime(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000000 + tv.tv_usec;
}
void rdma_connection::close_rdma_conn()
{
    //may have error
    /*if(conn_cq){
        if(ack_num != 0)
            ibv_ack_cq_events(conn_cq, ack_num);
        CCALL(ibv_destroy_cq(conn_cq));
        conn_cq = nullptr;
    }
    if(conn_comp_channel){
        CCALL(ibv_destroy_comp_channel(conn_comp_channel));
        conn_comp_channel = nullptr;
    };
    if(conn_pd){
        CCALL(ibv_dealloc_pd(conn_pd));
        conn_pd = nullptr;
    };
    ASSERT_RESULT(conn_id);ASSERT(conn_qp);
    rdma_destroy_qp(conn_id);conn_qp = nullptr;
    CCALL(rdma_destroy_id(conn_id));
    conn_id = nullptr;*/
}

void rdma_connection::build_qp_attr(struct ibv_qp_init_attr *qp_attr)
{
    memset(qp_attr, 0, sizeof(*qp_attr));
    qp_attr->send_cq = conn_cq;
    qp_attr->recv_cq = conn_cq;
    qp_attr->qp_type = IBV_QPT_RC;
    qp_attr->qp_context = (void*)this;
    qp_attr->sq_sig_all = 1;
    qp_attr->cap.max_inline_data = sizeof(message)+1;
    qp_attr->cap.max_send_wr = 2 * MAX_RECV_WR + 1;
    qp_attr->cap.max_recv_wr = 2 * MAX_RECV_WR + 1;
    qp_attr->cap.max_send_sge = MAX_SGE_NUM;
    qp_attr->cap.max_recv_sge = MAX_SGE_NUM;
}

//创建connection的相关资源包括pd,comp_channel, cq, qp
void rdma_connection::build_conn_res()
{
    ack_num = 0;
    ASSERT(conn_id->verbs);conn_ctx = conn_id->verbs;
    conn_pd = ibv_alloc_pd(conn_ctx); ASSERT(conn_pd);
    conn_comp_channel = ibv_create_comp_channel(conn_ctx); ASSERT(conn_comp_channel);
    conn_cq = ibv_create_cq(conn_ctx, CQE_MIN_NUM, this, conn_comp_channel, 0);
    ASSERT(conn_cq);
    CCALL(ibv_req_notify_cq(conn_cq, 0));
    
    /*将comm_comp_channel设置为非阻塞，并放置于_environment中的_efd_rdma_fd*/
    struct ibv_qp_init_attr qp_attr;
    build_qp_attr(&qp_attr);
    CCALL(rdma_create_qp(conn_id, conn_pd, &qp_attr));
    ASSERT(conn_id->qp); conn_qp = conn_id->qp;
    TRACE("Build rdma connection resource finished.\n");
   
    //注册MAX_RECV_WR用于接受控制信息的资源
    /*在创建完资源之后，需要进行post_receive操作，将用于接受的消息注册金qp中*/
    //Be awared of that qp size should >= MAX_RECV_WR (actually >= 2*MAX_RECV_WR)
    //why is MAX_RECV_WR+ 1? because the additional one post_recv for receiving MSG_STR and ACK_CLOSE
    for(int i = 0;i < MAX_RECV_WR +1 ;i++){
        post_new_recv_ctl_msg();
    }
    DEBUG("A connection  post %d recv_wr into qp.\n", MAX_RECV_WR);

    //将conn_comp_channel加入到_efd_rdma_fd
    MAKE_NONBLOCK(conn_comp_channel->fd);
    CCALL(ibv_req_notify_cq(conn_cq, 0));
    //conn_type是否已经初始化
    if(conn_type.owner == nullptr){
        conn_type = rdma_fd_data(this, conn_comp_channel->fd);
        DEBUG("Passive connection create rdma_fd_data.\n");
    }
    else {
        conn_type.fd = conn_comp_channel->fd;
        DEBUG("Active connetion update rdma_fd_data.\n");
    }
    epoll_event event;
    event.events   = EPOLLIN|EPOLLET;
    event.data.ptr = &conn_type;
    ASSERT(_environment);
    CCALL(epoll_ctl(((rdma_environment*)_environment)->_efd_rdma_fd, EPOLL_CTL_ADD, conn_comp_channel->fd, &event));
    DEBUG("A connection[passive/active] add conn_comp_channel->fd into _efd_rdma_fd.\n");
    
}

void rdma_connection::update_local_endpoint()
{
    struct sockaddr* local_sa = rdma_get_local_addr(conn_id);
    _local_endpoint.set_endpoint(local_sa);
}

void rdma_connection::update_remote_endpoint()
{
    struct sockaddr* remote_sa = rdma_get_peer_addr(conn_id);
    _remote_endpoint.set_endpoint(remote_sa);
}

void rdma_connection::process_established()
{
    //添加rundown相关函数
    connection_status original = _status.exchange(CONNECTION_CONNECTED);
    ASSERT(original == CONNECTION_CONNECTING);
    update_local_endpoint();
    if(OnConnect){
        OnConnect(this);
    }
    ASSERT(_status.load() == CONNECTION_CONNECTED);
    //此处应该有rundown.release, 因为connect已经完成了
    _rundown.release();
}

void rdma_connection::process_established_error()
{
    connection_status original = _status.exchange(CONNECTION_CONNECT_FAILED);
    ASSERT(original == CONNECTION_CONNECTING);
    if(OnConnectError){
        OnConnectError(this, errno);
    }
    _rundown.release();

}

bool rdma_connection::async_connect()
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return false;
    }

    connection_status expect = CONNECTION_NOT_CONNECTED;
    if (!_status.compare_exchange_strong(expect, CONNECTION_CONNECTING)) { 
        _rundown.release();
        return false;
    }

    ASSERT_RESULT(conn_id);
    CCALL(rdma_resolve_addr(conn_id, NULL, _remote_endpoint.data(), TIMEOUT_IN_MS));
    return true;
}

/*bool rdma_connection::async_close()
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return false;
    }

    if (!_rundown.shutdown()) {
        _rundown.release();
        return false;
    }
    ((rdma_environment*)_environment)->push_and_trigger_notification(rdma_event_data::rdma_connection_close(this));
    return true;
}*/

bool rdma_connection::async_close()
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return false;
    }

    if (!_rundown.shutdown()) {
        _rundown.release();
        return false;
    }
    if(_status.load() == CONNECTION_CONNECT_FAILED){
        ((rdma_environment*)_environment)->push_and_trigger_notification(rdma_event_data::rdma_fail_connection_close(this));
        return true;
    }
    ASSERT(_status.load() == CONNECTION_CONNECTED);
    self_ready_close.store(true);
    addr_mr *addr_mr_pair = addr_mr_pool.pop();
    message *ctl_msg      = ctl_msg_pool.pop();

    ctl_msg->type = message::ACK_CLOSE;
    struct ibv_mr *ctl_msg_mr = ibv_reg_mr(conn_pd, ctl_msg, sizeof(*ctl_msg), IBV_ACCESS_LOCAL_WRITE);
    ASSERT(ctl_msg_mr);
    addr_mr_pair->msg_addr = ctl_msg;
    addr_mr_pair->msg_mr   = ctl_msg_mr;

    struct ibv_send_wr wr, *bad_wr = nullptr; struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));
    sge.addr = (uintptr_t)ctl_msg;
    sge.length = sizeof(*ctl_msg);
    sge.lkey = ctl_msg_mr->lkey;

    wr.wr_id   = (uintptr_t)addr_mr_pair;
    wr.opcode  = IBV_WR_SEND;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_INLINE;

    CCALL(ibv_post_send(conn_qp, &wr, &bad_wr));
    DEBUG("Sending async_close req to the connection peer.\n");

    if(peer_ready_closed.load()){
        //wait for the IBV_WC_SEND has finished.
    }
    else{
        //wait for the IBV_WC_SEND and IBV_WC_RECV
    }

    //!!!!!remember _rundown.release() when receive the ack_close && self_ready_closed is true
    //if self_ready_closed is false. and receive the ack_close ,cannot release
    return true;
}

//assume the length is no more than 1G
bool rdma_connection::async_send(const void* buffer, const size_t length)
{
    ASSERT(buffer != nullptr); ASSERT(length > 0);
    //获取rundown
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return false;
    }

    connection_status cur_status = _status.load();
    if(cur_status != CONNECTION_CONNECTED){
        _rundown.release();
        return false;
    }

    size_t pre_queue_size = _sending_queue.size();
    //if peer_start_recv is false then only push send_buffer into _sending_queu
    //!!!!!!!!!!!在检查一遍,each element int the queue contains 2 sge
    size_t cur_len = length;
    const char* cur_send = (char*)buffer;
    void* send_start = (void*)const_cast<void*>(buffer);
    //WARN("send_start:%lld\n", (uintptr_t)send_start);
    size_t send_length = length; size_t sent_len = 0;
    int push_times = 0;

    while(cur_len > 0){
        push_times++;
        size_t reg_size;
        if(cur_len > MAX_SEND_LEN) reg_size = MAX_SEND_LEN;
        else reg_size = cur_len;
        //此pending_list需要放置到wr_id上，用于在完成操作后，ibv_dereg_mr
        rdma_sge_list *pending_list = new rdma_sge_list();//rdma_sge_pool.pop();
        rdma_sge_list::sge_info tmp_sge_info = {send_start, send_length, sent_len, false};
        //pending_list->send_start  = send_start;
        //pending_list->send_length = send_length;
        //pending_list->has_sent_len = sent_len;
        //注册内存
        ASSERT(conn_pd);

        struct ibv_mr *mr = ibv_reg_mr(conn_pd, (void*)const_cast<char*>(cur_send), reg_size, IBV_ACCESS_LOCAL_WRITE);
        pending_list->num_sge++;
        pending_list->total_length += reg_size;


        ASSERT(mr);
        //add a 16bytes to record recv_buffer addrs and recv_size
        struct ibv_sge tmp = {(uintptr_t)cur_send, (uint32_t)reg_size, mr->lkey};
        pending_list->sge_list.push_back(tmp);
        pending_list->mr_list.push_back(mr);
        //pending_list将其放入_sending_queue中
        _sending_queue.push(pending_list);
        TRACE("push %ld sge with total_length %ld into queue.\n",pending_list->num_sge, pending_list->total_length );
        //judge the whether send is finish

        if(cur_len < MAX_SEND_LEN) {
            tmp_sge_info.end = true;
            pending_list->sge_info_list.push_back(tmp_sge_info);
            break;
        }
        else{
            cur_len  -= MAX_SEND_LEN;
            if(cur_len == 0){
                tmp_sge_info.end = true;
                pending_list->sge_info_list.push_back(tmp_sge_info);
                break;
            }
            cur_send += MAX_SEND_LEN;
            sent_len += MAX_SEND_LEN;
            tmp_sge_info.end = false;
            pending_list->sge_info_list.push_back(tmp_sge_info);
        }

    }
    size_t new_size = _sending_queue.size();
    TRACE("push %d sending data into _sending_queue(now_size:%ld)\n", push_times, new_size);
   
    if(peer_start_recv.load()){
        if(pre_queue_size <= 0) {
            ((rdma_environment*)_environment)->
                    push_and_trigger_notification(rdma_event_data::rdma_async_send(this));
            TRACE("start_recv has ready.\n");
        }
    }
    else{
        TRACE("start_recv has not ready.\n");
    }
    return true;
}

//sending a message from
void rdma_connection::post_send_req_msg(rdma_sge_list* sge_list, bool isDecrease, addr_mr* reuse_addr_mr)
{
    //before send the req msg,we need post recv a msg for ack from peer;
    if(reuse_addr_mr)
        post_reuse_recv_ctl_msg(reuse_addr_mr);
    else
        post_new_recv_ctl_msg();

    addr_mr* addr_mr_pair = addr_mr_pool.pop();
    message *ctl_msg      = ctl_msg_pool.pop();
    struct ibv_mr *ctl_msg_mr = ibv_reg_mr(conn_pd, ctl_msg, sizeof(*ctl_msg),
                                           IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    ASSERT(ctl_msg_mr);
    addr_mr_pair->msg_addr = ctl_msg;
    addr_mr_pair->msg_mr   = ctl_msg_mr;

    struct ibv_send_wr wr, *bad_wr = nullptr; struct ibv_sge sge;
    memset(&wr, 0, sizeof(wr));
    ctl_msg->type = message::MSG_REQ;
    ctl_msg->data.peeding_send_size = sge_list->total_length;
    ctl_msg->send_ctx_addr = (uintptr_t)sge_list;//记录要发送的数据的位置，想不到更好的方法了

    sge.addr = (uintptr_t)ctl_msg;
    sge.length = sizeof(*ctl_msg);
    sge.lkey = ctl_msg_mr->lkey;

    wr.wr_id   = (uintptr_t)addr_mr_pair;
    wr.next    = nullptr;
    wr.opcode  = IBV_WR_SEND;
    wr.num_sge = 1;
    wr.sg_list = &sge;
    //wr.send_flags = IBV_SEND_INLINE;

    CCALL(ibv_post_send(conn_qp, &wr, &bad_wr));
    int cur_pwr;
    if(isDecrease){
        cur_pwr = --peer_rest_wr;
    }
    else cur_pwr = peer_rest_wr.load();
    DEBUG("Sending MSG_REQ to peer :request %lld size recv_buffer, send_ctx_addr is %lld (peer rest wr is >= %d).\n",
          (long long)ctl_msg->data.peeding_send_size,
          (long long)ctl_msg->send_ctx_addr, cur_pwr);

}

void rdma_connection::process_rdma_async_send()
{

    int cur_peer_rest_wr = peer_rest_wr.load();
    DEBUG("cur_peer_rest_wr is %d.\n", cur_peer_rest_wr);
    //tsqueue<rdma_sge_list*> _sending_queue;
    try_to_send();
}


void rdma_connection::try_to_send()
{
    while(peer_rest_wr.load()){
        rdma_sge_list *sge_list;
        bool issuc = _sending_queue.try_pop(&sge_list);
        if(!issuc) break;
        //发送请求
        post_send_req_msg(sge_list, true, NULL);
    }
}

void rdma_connection::process_poll_cq(struct ibv_cq *ret_cq, struct ibv_wc *ret_wc_array, int num_cqe)
{
    //WARN("num_cqe is %d\n", num_cqe); 
    for(int i = 0;i < num_cqe;i++){
        process_one_cqe(ret_wc_array+i);    
    }
}

void rdma_connection::post_send_ackctl_msg(message::msg_type type, uint64_t addr,
                                           uint32_t rkey, uintptr_t send_ctx_addr, uint32_t recv_addr_index)
{
    addr_mr *addr_mr_pair = addr_mr_pool.pop();
    message *send_ctl_msg = ctl_msg_pool.pop();
    struct ibv_mr *send_ctl_msg_mr = ibv_reg_mr(conn_pd, send_ctl_msg, sizeof(*send_ctl_msg),
                                                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    ASSERT(send_ctl_msg_mr);
    addr_mr_pair->msg_addr = send_ctl_msg;
    addr_mr_pair->msg_mr = send_ctl_msg_mr;

    send_ctl_msg->type = message::MSG_ACK;
    send_ctl_msg->data.mr.addr = addr;
    send_ctl_msg->data.mr.rkey = rkey;
    send_ctl_msg->send_ctx_addr= send_ctx_addr;
    send_ctl_msg->recv_addr_index = recv_addr_index;

    struct ibv_send_wr wr, *bad_wr = nullptr; struct ibv_sge sge;
    memset(&wr, 0, sizeof(wr));
    sge.addr = (uintptr_t) send_ctl_msg;
    sge.length = sizeof(*send_ctl_msg);
    sge.lkey = send_ctl_msg_mr->lkey;

    wr.wr_id = (uintptr_t) addr_mr_pair; 
    wr.opcode = IBV_WR_SEND;
    wr.next = nullptr; 
    wr.sg_list = &sge; 
    wr.num_sge = 1;
    //wr.send_flags = IBV_SEND_INLINE;
    CCALL(ibv_post_send(conn_qp, &wr, &bad_wr));
}

void rdma_connection::post_send_rdma_write(rdma_sge_list* pending_send_sge_list,
                                           uint64_t peer_addr, uint32_t peer_rkey,uint32_t imm_data_index)
{
    struct ibv_send_wr wr, *bad_wr = nullptr; struct ibv_sge sge;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uintptr_t)pending_send_sge_list;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.imm_data = htonl(imm_data_index);
    wr.wr.rdma.remote_addr = peer_addr;
    wr.wr.rdma.rkey = peer_rkey;
    wr.sg_list = &(pending_send_sge_list->sge_list[0]);
    wr.num_sge = pending_send_sge_list->num_sge;
    CCALL(ibv_post_send(conn_qp, &wr, &bad_wr));
}

void rdma_connection::post_new_recv_ctl_msg()
{
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    addr_mr* addr_mr_pair = addr_mr_pool.pop();
    //暂时每次pop的时候ibv_reg_mr，push的时候ibv_demsg_mr
    message* ctl_msg = ctl_msg_pool.pop();
    //WARN("sizeof message:%d\n", sizeof(int));
    ASSERT(conn_pd); ASSERT(ctl_msg);
    struct ibv_mr *ctl_msg_mr = ibv_reg_mr(conn_pd, ctl_msg, sizeof(*ctl_msg), 
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    ASSERT(ctl_msg_mr);
    addr_mr_pair->msg_addr = ctl_msg;
    addr_mr_pair->msg_mr   = ctl_msg_mr;

    memset(&wr, 0, sizeof(wr));
    sge.addr    = (uintptr_t)ctl_msg; 
    sge.length  = sizeof(*ctl_msg);
    sge.lkey    = ctl_msg_mr->lkey;

    wr.wr_id   = (uintptr_t)addr_mr_pair; 
    wr.sg_list = &sge;
    wr.num_sge = 1;

    CCALL(ibv_post_recv(conn_qp, &wr, &bad_wr));
}

void rdma_connection::post_reuse_recv_ctl_msg(addr_mr* reuse_addr_mr)
{
    ASSERT(reuse_addr_mr);
    struct ibv_recv_wr wr, *bad_wr = nullptr; struct ibv_sge sge;
    memset(&wr, 0, sizeof(wr));
    //memset(reuse_addr_mr->msg_addr, 0, sizeof(*reuse_addr_mr->msg_addr));
    sge.addr   = (uintptr_t)reuse_addr_mr->msg_addr;
    sge.length = sizeof(*reuse_addr_mr->msg_addr);
    sge.lkey   = reuse_addr_mr->msg_mr->lkey;
    wr.wr_id   = (uintptr_t)reuse_addr_mr;
    wr.next    = nullptr; wr.sg_list = &sge; wr.num_sge = 1;
    CCALL(ibv_post_recv(conn_qp, &wr, &bad_wr));
}

void rdma_connection::dereg_recycle(addr_mr* addr_mr_pair)
{
    ASSERT(addr_mr_pair);
    DEBUG("Execing dereg_recycle on addr_mr %lld .\n", (long long)addr_mr_pair);
    CCALL(ibv_dereg_mr(addr_mr_pair->msg_mr));
    ctl_msg_pool.push(addr_mr_pair->msg_addr);
    addr_mr_pool.push(addr_mr_pair);
}


void rdma_connection::process_one_cqe(struct ibv_wc *wc) {
    if (wc->status == IBV_WC_SUCCESS) {
        //判断是接受还是发送
        enum ibv_wc_opcode op = wc->opcode;
        switch (op) {
            case IBV_WC_RECV: {
                message *recv_ctl_msg = ((addr_mr*) wc->wr_id)->msg_addr;
                int recv_type = recv_ctl_msg->type;
                if(recv_type == message::MSG_REQ){
                    uintptr_t peer_send_ctx_addr = recv_ctl_msg->send_ctx_addr;
                    size_t recv_size = recv_ctl_msg->data.peeding_send_size;
                    char *recv_buffer = (char *)malloc(recv_size);
                    struct ibv_mr *recv_buffer_mr = ibv_reg_mr(conn_pd, recv_buffer, recv_size,
                                                               IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
                    //reuse recv_ctl_msg and arm the post recv before send_ackctl_msg
                    post_reuse_recv_ctl_msg((addr_mr*) wc->wr_id);
                    //post a new recv for IBV_WC_RECV_RDMA_WITH_IMM ,test is uint32_t wc->imm_data is zero;
                    post_new_recv_ctl_msg();

                    uint32_t index   = recvinfo_pool.pop();
                    recv_info* rinfo = recvinfo_pool.get(index);
                    rinfo->recv_addr = (uintptr_t)recv_buffer;
                    rinfo->recv_size = recv_size;
                    rinfo->recv_mr   = recv_buffer_mr;

                    post_send_ackctl_msg(message::MSG_ACK, (uintptr_t)recv_buffer_mr->addr,
                                         recv_buffer_mr->rkey, peer_send_ctx_addr, index);

                    DEBUG("Recv a req msg:(message_type:%d, req_size:%ld) and post two recv_msg .\n",
                          (int)recv_type, recv_size);

                }
                else if(recv_type == message::MSG_ACK){
                    //first post send the real msg that will be sent
                    rdma_sge_list*  pending_send_sge_list = (rdma_sge_list*)recv_ctl_msg->send_ctx_addr;
                    ASSERT(pending_send_sge_list);
                    uint64_t peer_addr = recv_ctl_msg->data.mr.addr;
                    uint32_t peer_rkey = recv_ctl_msg->data.mr.rkey;
                    uint32_t imm_data_index = recv_ctl_msg->recv_addr_index;
                    post_send_rdma_write(pending_send_sge_list, peer_addr, peer_rkey, imm_data_index);

                    DEBUG("Sending real message to peer (send_ctx_addr:%lld ; peer_addr:%lld ; peer_rkey:%lld, imm_data_index:%lld.\n", 
                            (long long)pending_send_sge_list, (long long)peer_addr, (long long)peer_rkey, (long long)imm_data_index);
                    //when recv a ack,_sending_queue is not empty, then send one, or peer_rest_wr++
                    rdma_sge_list *new_sge_list;
                    bool flag = _sending_queue.try_pop(&new_sge_list);
                    if(flag){
                        //this part we can reuse recv_ctl_msg
                        post_send_req_msg(new_sge_list, false, (addr_mr*) wc->wr_id);
                    }
                    else{
                        peer_rest_wr++;
                        //do something about ibv_dereg_mr and pool recycle;
                        dereg_recycle((addr_mr*) wc->wr_id);
                    }
                }
                else if(recv_type == message::MSG_STR){
                    DEBUG("connection recv start_receive request.\n");
                    post_reuse_recv_ctl_msg((addr_mr*) wc->wr_id);
                    //put the rdma_event_data into _notification_rdma_queue
                    peer_start_recv.store(true);
                    ((rdma_environment*)_environment)->
                            push_and_trigger_notification(rdma_event_data::rdma_async_send(this));
                }
                else if(recv_type == message::ACK_CLOSE){
                    DEBUG("connection recv ack_close request.\n");
                    post_reuse_recv_ctl_msg((addr_mr*)wc->wr_id);
                    peer_ready_closed.store(true);
                    if(!self_ready_close.load()){
                        //trigger Onhup
                        const int error = 0;
                        if(OnHup)
                            OnHup(this, error);
                    }
                    else{
                        if(self_ready_closed.load()){
                            //push to the close queue
                            ((rdma_environment*)_environment)->_ready_close_queue.
                                    push(close_conn_info(get_curtime(), this));
                            ((rdma_environment*)_environment)->
                                    push_and_trigger_notification(rdma_event_data::rdma_connection_close());
                            _rundown.release();
                        }
                        else{
                            //wait for IBV_WC_SEND has finished
                        }
                    }
                }
                else{
                    FATAL("Cannot handle recv_type %d.\n", (int)recv_type);
                    ASSERT(0);
                }
                break;
            }
            case IBV_WC_RECV_RDMA_WITH_IMM: {
                bool need_release;
                if (!_rundown.try_acquire(&need_release)) {
                    WARN("rdma_connection(conn_id=%ld) _rundown.try_acquire() failed\n", (uintptr_t)conn_id);
                    _rundown.release();
                    return;
                }

                uint32_t recv_index = ntohl(wc->imm_data);
                recv_info* rinfo = recvinfo_pool.get(recv_index);
                DEBUG("ready to onReceive ,have receive %ld size buffer on addr %ld (recv_index : %d).\n",
                      rinfo->recv_size, (uintptr_t)rinfo->recv_addr, recv_index);
                if(rinfo->recv_size > 0){
                    if(OnReceive)
                        OnReceive(this, (void*)rinfo->recv_addr, rinfo->recv_size);
                }
                //this recv msg which be registered must be deregister,and
                CCALL(ibv_dereg_mr(rinfo->recv_mr));
                free((char*)rinfo->recv_addr);
                recvinfo_pool.push(recv_index);
                _rundown.release();
                break;
            }
            case IBV_WC_SEND: {
                //!!!!! whether is close
                addr_mr* send_addr_mr = (addr_mr*) wc->wr_id;
                ASSERT(send_addr_mr);
                message* msg_addr = send_addr_mr->msg_addr;
                TRACE("Have complete a msg send with addr : %ld, type = %d\n", (uintptr_t)msg_addr, msg_addr->type);
                if(msg_addr->type == message::ACK_CLOSE){
                    DEBUG("The ack_close message have already send.\n");
                    self_ready_closed.store(true);
                    if(peer_ready_closed.load()){
                        WARN("xxxxxxxxxxxxxxxxx\n");
                        //push to close queue
                        ((rdma_environment*)_environment)->_ready_close_queue.
                                push(close_conn_info(get_curtime(), this));
                        ((rdma_environment*)_environment)->
                                push_and_trigger_notification(rdma_event_data::rdma_connection_close());
                        _rundown.release();
                    }
                    else{
                        //wait for IBV_WC_RECV
                    }
                }

                //deregister and recycle pool
                CCALL(ibv_dereg_mr(send_addr_mr->msg_mr));
                ctl_msg_pool.push(send_addr_mr->msg_addr);
                addr_mr_pool.push(send_addr_mr);
                //try to continue send
                try_to_send();
                break;
            }
            case IBV_WC_RDMA_WRITE: {
                rdma_sge_list* sge_list = (rdma_sge_list*)wc->wr_id;
                ASSERT(sge_list);
                //regard as sge_list has more than one sge
                for(int i = 0; i < sge_list->num_sge;++i){
                    if(sge_list->sge_info_list[i].end){
                        TRACE("Have complete a buffer send with addr : %ld, sge_list->num_sge = %ld lenght %d \n",
                              (uintptr_t)sge_list->sge_info_list[i].send_start, sge_list->num_sge, sge_list->sge_info_list[i].send_length);
                        if(OnSend){
                            ASSERT(sge_list->sge_info_list[i].send_length);
                            ERROR("xxxxxxxxxxxxxxxxxxxxxxxxx\n");
                            OnSend(this, sge_list->sge_info_list[i].send_start, sge_list->sge_info_list[i].send_length);
                        }
                        _rundown.release();
                    }
                }
                /*if(sge_list->end){
                    TRACE("Have complete a buffer send with addr : %ld.\n",(uintptr_t)sge_list->send_start);
                    if(OnSend) {
                        ASSERT(sge_list->send_length);
                        OnSend(this, sge_list->send_start, sge_list->send_length);
                    }
                    _rundown.release();
                }*/
                //deregister mr
                for(auto mr:sge_list->mr_list)
                    CCALL(ibv_dereg_mr(mr));
                //sge_list->sge_list.clear();
                //sge_list->mr_list.clear();
                //sge_list->sge_info_list.clear();
                delete sge_list;
                //rdma_sge_pool.push(sge_list);
                break;
            }
            default: {
                FATAL("cannot handle ibv_wc_opcode %d.\n", (int)op);
                ASSERT(0);
                break;
            }
        }
    }
    //handle error message, remember to capture IBV_WC_RNR_RETRY_EXC_ERR event, this means program has bug
    else{
        ERROR("process_one_cqe error.\n");
        enum ibv_wc_opcode op = wc->opcode;
        if(op == IBV_WC_RDMA_WRITE){
            rdma_sge_list* sge_list = (rdma_sge_list*)wc->wr_id;
            ASSERT(sge_list);
            for(rdma_sge_list::sge_info &element:sge_list->sge_info_list){
                if(OnSendError){
                    TRACE("happen a senderror when sending buffer %ld (error status : %s\n",
                          (uintptr_t)element.send_start,ibv_wc_status_str(wc->status));
                    const int error = errno;
                    OnSendError(this, element.send_start, element.send_length, element.has_sent_len, error);
                }
                if(element.end) _rundown.release();
            }
            /*if(OnSendError){
                TRACE("happen a senderror when sending buffer %ld (error status : %s\n",
                      (uintptr_t)sge_list->send_start, ibv_wc_status_str(wc->status));
                const int error = errno;
                OnSendError(this, sge_list->send_start, sge_list->send_length, sge_list->has_sent_len, error);
            }
            if(sge_list->end) _rundown.release();*/
        }

        //trigger Onhup?
        if(OnHup){
            TRACE("happen a hup with error_status : %s\n", ibv_wc_status_str(wc->status));
            const int error = errno;
            OnHup(this, error);
        }

        FATAL("The program may has bug.or the You need to review your code(status:%s).\n",
                  ibv_wc_status_str(wc->status));
        //below code may need to be delete
        if(wc->status & IBV_WC_RNR_RETRY_EXC_ERR) {
            if(_close_finished){
                TRACE("The connection has already close.\n");
                return;
            }
            //FATAL("The program may has bug.or the You need to review your code(status:%s).\n",  ibv_wc_status_str(wc->status));

            //ASSERT(0);
        }
    }
}

bool rdma_connection::start_receive()
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return false;
    }

    if (_status != CONNECTION_CONNECTED) {
        _rundown.release();
        return false;
    }
    addr_mr *addr_mr_pair = addr_mr_pool.pop();
    message *ctl_msg      = ctl_msg_pool.pop();

    ctl_msg->type = message::MSG_STR;
    struct ibv_mr *ctl_msg_mr = ibv_reg_mr(conn_pd, ctl_msg, sizeof(*ctl_msg), IBV_ACCESS_LOCAL_WRITE);
    ASSERT(ctl_msg_mr);
    addr_mr_pair->msg_addr = ctl_msg;
    addr_mr_pair->msg_mr   = ctl_msg_mr;

    struct ibv_send_wr wr, *bad_wr = nullptr; struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));
    sge.addr = (uintptr_t)ctl_msg;
    sge.length = sizeof(*ctl_msg);
    sge.lkey = ctl_msg_mr->lkey;

    wr.wr_id   = (uintptr_t)addr_mr_pair; //wr.next    = nullptr;
    wr.opcode  = IBV_WR_SEND;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_INLINE;

    CCALL(ibv_post_send(conn_qp, &wr, &bad_wr));
    DEBUG("Sending start_receive req to the connection peer.\n");
    _rundown.release();
    return true;
}

bool rdma_connection::async_send_many(const std::vector<fragment> frags)
{
    ASSERT(!frags.empty());
#ifndef NDEBUG
    for (const fragment& frag : frags) {
        ASSERT(frag.original_buffer() != nullptr);
        ASSERT(frag.original_length() != 0);
    }
#endif
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return false;
    }

    // Increase _rundown count as if we call async_send() so many times
    for (size_t i = 0; i < frags.size() - 1; ++i) {
        bool success = _rundown.try_acquire(&need_release);
        ASSERT(success);
        ASSERT(need_release);
    }

    size_t pre_queue_size = _sending_queue.size();

    int num = frags.size();int pushtime = 0;
    rdma_sge_list *pending_list = new rdma_sge_list();
    size_t left_size = MAX_SEND_LEN - pending_list->total_length;
    DEBUG("Initial left_size = %lld\n", (long long)left_size);
    int i = 0;
    size_t left_frag_len = frags[i].original_length();
    char* cur_send = (char*)const_cast<void*>(frags[i].original_buffer());
    void* send_start = const_cast<void*>(frags[i].original_buffer());
    size_t send_length = frags[i].original_length();
    size_t sent_len = 0;


    for(;i < num;){
        while(left_size > 0){
            if(left_frag_len <= left_size){
                rdma_sge_list::sge_info tmp_sge_info = {send_start, send_length, sent_len, true};
                struct ibv_mr *mr = ibv_reg_mr(conn_pd, (void*)(cur_send), left_frag_len, IBV_ACCESS_LOCAL_WRITE);
                ASSERT(mr);
                pending_list->num_sge++;
                pending_list->total_length += left_frag_len;

                struct ibv_sge tmp = {(uintptr_t)cur_send, (uint32_t)left_frag_len, mr->lkey};
                pending_list->sge_list.push_back(tmp);
                pending_list->mr_list.push_back(mr);
                pending_list->sge_info_list.push_back(tmp_sge_info);

                left_size -= left_frag_len;

                i++;
                if(i < num){
                    left_frag_len = frags[i].original_length();
                    cur_send = (char*)const_cast<void*>(frags[i].original_buffer());
                    send_start = const_cast<void*>(frags[i].original_buffer());
                    send_length = frags[i].original_length();
                    sent_len = 0;
                }
                break;
            }
            else{
                rdma_sge_list::sge_info tmp_sge_info = {send_start, send_length, sent_len, false};
                struct ibv_mr *mr = ibv_reg_mr(conn_pd, (void*)(cur_send), left_size, IBV_ACCESS_LOCAL_WRITE);
                ASSERT(mr);
                pending_list->num_sge++;
                pending_list->total_length += left_size;

                struct ibv_sge tmp = {(uintptr_t)cur_send, (uint32_t)left_size, mr->lkey};
                pending_list->sge_list.push_back(tmp);
                pending_list->mr_list.push_back(mr);
                pending_list->sge_info_list.push_back(tmp_sge_info);

                cur_send += left_size;
                left_frag_len -= left_size;
                sent_len += left_size;
                left_size = 0;
                ERROR("%lld\n", (long long)sent_len);
                break;
            }
        }

        if(left_size == 0){
            pushtime++;
            DEBUG("[async_send_many] push %d time: num_sge=%d, total_length=%lld.\n",
                  pushtime, (int)pending_list->num_sge, (long long)pending_list->total_length);
            for(int k = 0;k < pending_list->num_sge;++k){
                DEBUG("[detail_sge %d]: sge_len = %lld, has_sent_len = %lld, is_end = %d\n",k,
                      (long long)pending_list->sge_list[k].length,
                      (long long)pending_list->sge_info_list[k].has_sent_len,
                      (int)pending_list->sge_info_list[k].end);
            }
            _sending_queue.push(pending_list);
            DEBUG("sent_len:%lld\n", (long long)sent_len);
            pending_list = new rdma_sge_list();
            left_size = MAX_SEND_LEN - pending_list->total_length;
        }
    }
    if(left_size != 0){
        pushtime++;
        DEBUG("[async_send_many != 0] push %d time: num_sge=%d, total_length=%lld.\n",
              pushtime, (int)pending_list->num_sge, (long long)pending_list->total_length);
        for(int k = 0;k < pending_list->num_sge;++k){
            DEBUG("[detail_sge %d]: sge_len = %lld, has_sent_len = %lld, is_end = %d\n",k,
                  (long long)pending_list->sge_list[k].length,
                  (long long)pending_list->sge_info_list[k].has_sent_len,
                  (int)pending_list->sge_info_list[k].end);
        }
        _sending_queue.push(pending_list);
    }

    size_t new_size = _sending_queue.size();
    TRACE("[async_send_many] push %d sending data into _sending_queue(now_size:%ld)\n", pushtime, new_size);

    if(peer_start_recv.load()){
        if(pre_queue_size <= 0) {
            ((rdma_environment*)_environment)->
                    push_and_trigger_notification(rdma_event_data::rdma_async_send(this));
            TRACE("[async_send_many] start_recv has ready.\n");
        }
    }
    else{
        TRACE("[async_send_many]start_recv has not ready.\n");
    }

    return true;
}
