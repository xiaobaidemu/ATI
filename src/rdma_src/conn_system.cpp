#ifdef IBEXIST
#include "conn_system.h"
#include "rdma_resource.h"
#include <functional>
#define IP_LEN 16
conn_system::~conn_system() {
    WARN("Transfer System is ready to close.\n");
    env.dispose();
    issend_running = false;
    isrecv_running = false;
    poll_send_thread->join();
    poll_recv_thread->join();
}

void conn_system::splitkey(const std::string& s, std::string& ip, int &port, const std::string& c)
{
    std::string::size_type pos1, pos2;
    pos2 = s.find(c); pos1 = 0;
    ip = s.substr(pos1, pos2-pos1);
    pos1 = pos2 + c.size();
    port = atoi(s.substr(pos1).c_str());
}

conn_system::conn_system(const char *ip, int port) {
    issend_running  = true;
    isrecv_running  = true;
    this->my_listen_ip = (char*)malloc(IP_LEN);
    strcpy(this->my_listen_ip, ip);
    this->my_listen_port = port;

    int num_devices;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        ERROR("Failed to get IB devices list\n");
        ASSERT(0);
    }
    struct ibv_device *ibv_dev;
    for (int i = 0; i < num_devices; i++) {
        ibv_dev = dev_list[i];
        context = ibv_open_device(ibv_dev);
        if(context) break;
    }

    rx_depth = RX_DEPTH;
    ib_port = 1;//default is 1
    if(!(context)) {
        ERROR("Cannot open any ibv_device.\n");
        ASSERT(0);
    }

    CCALL(ibv_query_port(context, ib_port, &(portinfo)));
    channel = ibv_create_comp_channel(context); ASSERT(channel);
    pd = ibv_alloc_pd(context); ASSERT(pd);
    cq_send_qp = ibv_create_cq(context, rx_depth*10, this, channel, 0); ASSERT(cq_send_qp);
    cq_recv_qp = ibv_create_cq(context, rx_depth*10, this, channel, 0); ASSERT(cq_recv_qp);
    SUCC("[%s:%d] CREATE CQ FINISHED.\n", my_listen_ip, my_listen_port);

    lis = env.create_listener(my_listen_ip, my_listen_port);
    lis->OnAccept = [&](listener*,  connection* conn){
        set_passive_connection_callback(conn);
        conn->start_receive();
        IDEBUG("[OnAccept] \n");
    };
    lis->OnAcceptError = [&](listener*, const int error){
        ERROR("[%s:%d] OnAcceptError......\n", ip, port);
    };
    lis->OnClose = [&](listener*){
        DEBUG("You can deal with it on close.\n");
    };
    bool success = lis->start_accept();
    ASSERT(success);
    run_poll_thread();
}

void conn_system::poll_send_func() {
    struct ibv_wc wc[RX_DEPTH+1];
    int n; bool ret;
    while(issend_running){
        n = ibv_poll_cq(cq_send_qp, RX_DEPTH+1, wc);
        if(n < 0){
            ERROR("some error when poll send_rdma_conn.cq.\n");
            return;
        }
        if(n > 0){
            ITR_SPECIAL("ibv_poll_cq send_num:%d.\n", n);
            ret = do_send_completion(n, wc);
            ASSERT(ret);
        }
    }
}

void conn_system::poll_recv_func() {
    struct ibv_wc wc[RX_DEPTH+1];
    int n; bool ret;
    while(isrecv_running){
        n = ibv_poll_cq(cq_recv_qp, RX_DEPTH+1, wc);
        if(n < 0){
            ERROR("some error when poll recv_rdma_conn.cq.\n");
            return;
        }
        if(n > 0){
            ITR_SPECIAL("ibv_poll_cq recv_num:%d.\n", n);
            ret = do_recv_completion(n, wc);
            ASSERT(ret);
        }
    }
}

void conn_system::run_poll_thread() {
    poll_send_thread = new std::thread(std::bind(&conn_system::poll_send_func, this));
    poll_recv_thread = new std::thread(std::bind(&conn_system::poll_recv_func, this));
}

async_conn_p2p* conn_system::init(char* peer_ip, int peer_port)
{
    //judge whether the rdma_conn_p2p *ret has already existed
    std::string key = std::string(peer_ip) + "_" + std::to_string(peer_port);
    rdma_conn_p2p *conn_object = nullptr;
    if(connecting_map.InContains(key)){
        ASSERT(connecting_map.Get(key, &conn_object));
        ASSERT(conn_object);
    }
    else{
        _double_check_lock.acquire();
        if(!connecting_map.InContains(key)){
            conn_object = new rdma_conn_p2p();
            conn_object->conn_sys = this;
            conn_object->send_rdma_conn.ib_port  = this->ib_port;
            conn_object->send_rdma_conn.portinfo = this->portinfo;
            conn_object->recv_rdma_conn.ib_port  = this->ib_port;
            conn_object->recv_rdma_conn.portinfo = this->portinfo;
            connecting_map.Set(key, conn_object);
        }
        else{
            ASSERT(connecting_map.Get(key, &conn_object));
        }
        _double_check_lock.release();
    }
    ASSERT(conn_object);
    
    socket_connection *send_conn = env.create_connection(peer_ip, peer_port);
    set_active_connection_callback(send_conn, key);
    send_conn->async_connect();
    IDEBUG("Ready to establish with %s\n", key.c_str());

    conn_object->_lock_send_ech.acquire();
    //fill the conn_object for send_direction
    exchange_qp_data send_direction_data = conn_object->send_direction_qp;
    SUCC("[%s] SEND_direction my_qp_info:   LID 0x%04x, QPN 0x%06x\n", key.c_str(),
         conn_object->send_rdma_conn.portinfo.lid, conn_object->send_rdma_conn.qp->qp_num);
    SUCC("[%s] SEND_direction peer_qp_info: LID 0x%04x, QPN 0x%06x\n", key.c_str(),
         send_direction_data.lid, send_direction_data.qpn);
    recvd_buf_info &tmp_send_info = conn_object->send_peer_buf_status.buf_info;
    SUCC("[%s] SEND_direction send_peer_buf_status: addr 0x%llx, rkey 0x%llx, size 0x%llx, mr 0x%llx\n", key.c_str(),
         (long long)tmp_send_info.addr, (long long)tmp_send_info.rkey, (long long)tmp_send_info.size, (long long)tmp_send_info.buff_mr);

    conn_object->_lock_recv_ech.acquire();
    exchange_qp_data recv_direction_data = conn_object->recv_direction_qp;

    SUCC("[%s] RECV_direction my_qp_info:   LID 0x%04x, QPN 0x%06x\n", key.c_str(),
         conn_object->recv_rdma_conn.portinfo.lid, conn_object->recv_rdma_conn.qp->qp_num);
    SUCC("[%s] RECV_direction peer_qp_info: LID 0x%04x, QPN 0x%06x\n", key.c_str(),
         recv_direction_data.lid, recv_direction_data.qpn);
    recvd_buf_info &tmp_recv_info = conn_object->recv_local_buf_status.buf_info;
    SUCC("[%s] RECV_direction recv_local_buf_status: addr 0x%llx, rkey 0x%llx, size 0x%llx, mr 0x%llx\n", key.c_str(),
         (long long)tmp_recv_info.addr, (long long)tmp_recv_info.rkey, (long long)tmp_recv_info.size, (long long)tmp_recv_info.buff_mr);

    SUCC("[===== %s_%d FINISH INIT TO %s.=====]\n", my_listen_ip, my_listen_port, key.c_str());
    conn_object->clean_used_fd();
    return conn_object;
}

void conn_system::set_active_connection_callback(connection *send_conn, std::string key) {
    send_conn->OnConnect = [key, this](connection* conn) {
        rdma_conn_p2p *key_object = nullptr;
        ASSERT(connecting_map.Get(key, &key_object));
        key_object->send_socket_conn = (socket_connection*)conn;
        conn->start_receive();
        //you need create a new qp
        key_object->create_qp_info(key_object->send_rdma_conn, false);

        ctl_data *my_qp_info = new ctl_data();
        strcpy(my_qp_info->ip, my_listen_ip);
        my_qp_info->port = my_listen_port;
        my_qp_info->type = HEAD_TYPE_EXCH;
        my_qp_info->qp_info.lid = htons(key_object->send_rdma_conn.portinfo.lid);
        my_qp_info->qp_info.qpn = htonl(key_object->send_rdma_conn.qp->qp_num);
        conn->async_send(my_qp_info, sizeof(ctl_data));
        WARN("ready to send qp_info to %s (len:%d).\n", key.c_str(), (int)sizeof(ctl_data));
    };

    send_conn->OnConnectError = [key, this](connection *conn, const int error){
        conn->async_close();
        std::string peer_ip; int peer_port;
        splitkey(key, peer_ip, peer_port, "_");
        socket_connection * newconn = env.create_connection(peer_ip.c_str(), peer_port);
        newconn->OnConnectError = conn->OnConnectError;
        newconn->OnConnect = conn->OnConnect;
        newconn->OnHup = conn->OnHup;
        newconn->OnClose = conn->OnClose;
        newconn->OnSend = conn->OnSend;
        newconn->OnSendError = conn->OnSendError;
        newconn->OnReceive   = conn->OnReceive;
        usleep(10);
        ASSERT(newconn->async_connect());
        IDEBUG("try to connect %s again.\n", key.c_str());
    };

    send_conn->OnSend = [key, this](connection* conn, const void* buffer, const size_t length){
        if(length == sizeof(ctl_data)){
            IDEBUG("Have already send the qp_info to %s.\n", key.c_str());
            delete (ctl_data*)const_cast<void*>(buffer);
        }
        else if(length ==sizeof("done")){
            IDEBUG("Have already send the DONE to %s.\n", key.c_str());
            rdma_conn_p2p *pending_conn;
            connecting_map.Get(key, &pending_conn); ASSERT(pending_conn);
            pending_conn->send_direction_qp.lid = ntohs(conn->cur_recv_info.qp_all_info.qp_info.lid);
            pending_conn->send_direction_qp.qpn = ntohl(conn->cur_recv_info.qp_all_info.qp_info.qpn);

            pending_conn->send_peer_buf_status.buf_info.addr = ntohll(conn->cur_recv_info.qp_all_info.peer_buf_info.addr);
            pending_conn->send_peer_buf_status.buf_info.rkey = ntohl(conn->cur_recv_info.qp_all_info.peer_buf_info.rkey);
            pending_conn->send_peer_buf_status.buf_info.size = ntohll(conn->cur_recv_info.qp_all_info.peer_buf_info.size);
            pending_conn->send_peer_buf_status.buf_info.buff_mr = ntohll(conn->cur_recv_info.qp_all_info.peer_buf_info.buff_mr);

            //modify the qp status
            pending_conn->modify_qp_to_rtr(pending_conn->send_rdma_conn.qp, pending_conn->send_direction_qp.qpn,
                                           pending_conn->send_direction_qp.lid, pending_conn->send_rdma_conn.ib_port);
            pending_conn->modify_qp_to_rts(pending_conn->send_rdma_conn.qp);
            pending_conn->_lock_send_ech.release();
            //WARN("nofity_system send_event_fd:%d\n",pending_conn->send_event_fd);
        }

    };
    send_conn->OnHup = [key, this](connection* conn, const int error){
        if (error == 0) {
            TRACE("[OnHup] Because rank %s is close normally...\n", key.c_str());
        }
        else {
            ERROR("[OnHup] Because rank %s is close Abnormal...\n", key.c_str());
        }
    };

    send_conn->OnClose = [key, this](connection* conn){
        if(((socket_connection*)conn)->get_conn_status() == CONNECTION_CONNECT_FAILED)
            DEBUG("the failed connection close %s.\n", key.c_str());
        else
            DEBUG("send_conn close normally.\n");
    };

    send_conn->OnSendError = [key, this](connection* conn, const void* buffer, const size_t length, const size_t sent_length, const int error) {
        ERROR("[%s] OnSendError: %d (%s). all %lld, sent %lld\n",
              key.c_str(), error, strerror(error), (long long)length, (long long)sent_length);
        ASSERT(0);
    };

    send_conn->OnReceive = [key, this](connection* conn, const void* buffer, const size_t length){
        //we should the buffer whose length is equal to length
        size_t cur_recvd = conn->cur_recv_info.recvd_size;
        if(cur_recvd + length < sizeof(ctl_data)){
            memcpy(&conn->cur_recv_info.qp_all_info+cur_recvd, buffer, length);
            conn->cur_recv_info.recvd_size += length;
        }
        else{
            ASSERT(cur_recvd + length == sizeof(ctl_data));
            memcpy(&conn->cur_recv_info.qp_all_info+cur_recvd, buffer, length);
            conn->cur_recv_info.recvd_size += length;
            conn->async_send("done", sizeof("done"));
            IDEBUG("RECVed the qp_info from %s, and ready to send done.\n", key.c_str());

            std::string peer_key = std::string(conn->cur_recv_info.qp_all_info.ip) + "_" +
                    std::to_string(conn->cur_recv_info.qp_all_info.port);
            ASSERT(conn->cur_recv_info.qp_all_info.type == HEAD_TYPE_EXCH);
            ASSERT(peer_key == key);

            
        }
    };

}

void conn_system::set_passive_connection_callback(connection *recv_conn) {
    /* you also need create a new qp*/
    recv_conn->OnSend = [this](connection* conn, const void* buffer, const size_t length){
        ASSERT(length == sizeof(ctl_data));
        IDEBUG("recv_conn has already finished send my qp information.\n");
        delete (ctl_data*)const_cast<void*>(buffer);
    };
    recv_conn->OnHup = [this](connection* conn, const int error){
        if (error == 0) TRACE("[OnHup] recv_conn close normally.\n");
        else ERROR("[OnHup] recv_conn close Abnormal...\n");
    };

    recv_conn->OnClose = [this](connection* conn){
        DEBUG("[OnClose] recv_conn close normally.\n");
    };

    recv_conn->OnSendError = [this](connection* conn, const void* buffer, const size_t length, const size_t sent_length, const int error) {
        ERROR("[OnSendError]:recv_conn  %d (%s). all %lld, sent %lld\n",
               error, strerror(error), (long long)length, (long long)sent_length);
        ASSERT(0);
    };

    recv_conn->OnReceive = [this](connection* conn, const void* buffer, const size_t length){
        size_t cur_recvd = conn->cur_recv_info.recvd_size;
        if(cur_recvd == sizeof(ctl_data)){
            IDEBUG("Peer has already recv the qp info from me.(%s)\n", (char*) const_cast<void*>(buffer));
             std::string peer_key = std::string(conn->cur_recv_info.qp_all_info.ip) + "_" +
                                       std::to_string(conn->cur_recv_info.qp_all_info.port);

            rdma_conn_p2p *conn_object = nullptr;
            ASSERT(connecting_map.Get(peer_key, &conn_object));
            conn_object->_lock_recv_ech.release();
        }
        else{
            if(cur_recvd + length < sizeof(ctl_data)){
                memcpy(&conn->cur_recv_info.qp_all_info+cur_recvd, buffer, length);
                conn->cur_recv_info.recvd_size += length;
            }
            else{
                ASSERT(cur_recvd + length == sizeof(ctl_data));
                memcpy(&conn->cur_recv_info.qp_all_info+cur_recvd, buffer, length);
                conn->cur_recv_info.recvd_size += length;
                ASSERT(conn->cur_recv_info.qp_all_info.type == HEAD_TYPE_EXCH);
                std::string peer_key = std::string(conn->cur_recv_info.qp_all_info.ip) + "_" +
                                       std::to_string(conn->cur_recv_info.qp_all_info.port);

                rdma_conn_p2p *conn_object = nullptr;
                if(connecting_map.InContains(peer_key)){
                    ASSERT(connecting_map.Get(peer_key, &conn_object));
                    ASSERT(conn_object);
                }
                else{
                    _double_check_lock.acquire();
                    if(!connecting_map.InContains(peer_key)){
                        conn_object = new rdma_conn_p2p();
                        conn_object->conn_sys = this;
                        conn_object->send_rdma_conn.ib_port  = this->ib_port;
                        conn_object->send_rdma_conn.portinfo = this->portinfo;
                        conn_object->recv_rdma_conn.ib_port  = this->ib_port;
                        conn_object->recv_rdma_conn.portinfo = this->portinfo;
                        connecting_map.Set(peer_key, conn_object);
                    }
                    else{
                        ASSERT(connecting_map.Get(peer_key, &conn_object));
                    }
                    _double_check_lock.release();
                }
                ASSERT(conn_object);
                conn_object->recv_direction_qp.lid = ntohs(conn->cur_recv_info.qp_all_info.qp_info.lid);
                conn_object->recv_direction_qp.qpn = ntohl(conn->cur_recv_info.qp_all_info.qp_info.qpn);
                if(conn_object->conn_sys == nullptr){
                    ITRACE("conn_object->conn_sys is nullptr now.\n");
                    ERROR("this won't happened.\n");ASSERT(0); 
                }
                //ready to send my qp information

                conn_object->create_qp_info(conn_object->recv_rdma_conn, true);
                conn_object->recv_socket_conn = (socket_connection*)conn;

                ctl_data *my_qp_info = new ctl_data();
                strcpy(my_qp_info->ip, my_listen_ip);
                my_qp_info->port = my_listen_port;
                my_qp_info->type = HEAD_TYPE_EXCH;
                my_qp_info->qp_info.lid = htons(conn_object->recv_rdma_conn.portinfo.lid);
                my_qp_info->qp_info.qpn = htonl(conn_object->recv_rdma_conn.qp->qp_num);

                my_qp_info->peer_buf_info.addr = htonll(conn_object->recv_local_buf_status.buf_info.addr);
                my_qp_info->peer_buf_info.rkey = htonl(conn_object->recv_local_buf_status.buf_info.rkey);
                my_qp_info->peer_buf_info.size = htonll(conn_object->recv_local_buf_status.buf_info.size);
                my_qp_info->peer_buf_info.buff_mr = htonll(conn_object->recv_local_buf_status.buf_info.buff_mr);
                conn->async_send(my_qp_info, sizeof(ctl_data));
            }
        }
    };
}

bool conn_system::do_send_completion(int n, struct ibv_wc *wc_send){
    for(int i = 0;i < n;i++){
        struct ibv_wc *wc = wc_send + i;
        if(wc->status != IBV_WC_SUCCESS){
            ERROR("some error when do_send_completion (%s).\n",ibv_wc_status_str(wc->status));
            return false;
        }
        enum ibv_wc_opcode op = wc->opcode;
        if(op == IBV_WC_SEND)
            continue;
        if(op == IBV_WC_RECV) {
            mr_pair_recv *tmp_id = (mr_pair_recv*)(wc->wr_id);
            struct ibv_qp *my_qp = tmp_id->which_qp;
            struct ibv_mr *recv_mr = tmp_id->recv_mr;
            ctl_flow_info *ack_ctl_info = (ctl_flow_info *)(recv_mr->addr);
            rdma_conn_p2p* my_conn_p2p = (rdma_conn_p2p*)my_qp->qp_context;
            my_conn_p2p->recv_mr_pool.push(tmp_id);
            if (ack_ctl_info->type == 0) {
                int tmp_used_recv = ack_ctl_info->ctl.used_recv_num;
                size_t tmp_recvd_bufsize = ack_ctl_info->ctl.recvd_bufsize;
                //change the send_peer_buf_status.pos_irecv

                my_conn_p2p->send_peer_buf_status.pos_irecv = (my_conn_p2p->send_peer_buf_status.pos_irecv + tmp_recvd_bufsize)
                                                              %my_conn_p2p->send_peer_buf_status.buf_info.size;

                //recycle the recv_mr !!!
                //memset(ack_ctl_info, 0, sizeof(ctl_flow_info));
                CCALL(my_conn_p2p->pp_post_recv(my_qp, (uintptr_t)ack_ctl_info, recv_mr->lkey,
                                   sizeof(ctl_flow_info), recv_mr));

                my_conn_p2p->_lock_for_peer_num.acquire();
                my_conn_p2p->peer_left_recv_num += tmp_used_recv;
                //WARN("peer_left_recv_num: %d.\n", my_conn_p2p->peer_left_recv_num);
                while(!my_conn_p2p->unsend_queue.empty()){
                    if(my_conn_p2p->peer_left_recv_num > 0){
                        my_conn_p2p->peer_left_recv_num--;
                        unsend_element element = my_conn_p2p->unsend_queue.front();
                        my_conn_p2p->unsend_queue.pop();
                        if(element.is_real_msg){
                            CCALL(my_conn_p2p->pp_post_write(element.real_msg_info.mr_pair, element.real_msg_info.remote_addr,
                                                element.real_msg_info.rkey, element.real_msg_info.imm_data));
                            ITR_POLL("(sending real big/small msg from unsend_queue): recv_buffer %llx, rkey %x, len %d\n",
                                     (long long)element.real_msg_info.remote_addr, element.real_msg_info.rkey, element.real_msg_info.mr_pair->len);
                        }
                        else{
                            CCALL(my_conn_p2p->pp_post_send(my_qp, (uintptr_t)&(element.req_msg_info.req_msg), 0, sizeof(send_req_clt_info), true, false));
                            ITR_SEND("(BIG_MSG_REQ sending from unsend_queue): send_addr %llx, len %d, isend_index %d\n",(long long)element.req_msg_info.req_msg.send_mr->addr,
                                     (int)element.req_msg_info.req_msg.send_mr->length, element.req_msg_info.req_msg.isend_index);
                        }
                        //WARN("xxx peer_left_recv_num :%d size of unsend_queue:%d.\n", peer_left_recv_num, unsend_queue.size());
                    }
                    else break;
                }
                my_conn_p2p->_lock_for_peer_num.release();

            } else {
                ASSERT(ack_ctl_info->type);
                if(ack_ctl_info->big.is_oneside){
                    ITR_POLL("this ack_ctl_info is a one_side pre msg from recv_peer.\n");
                    //restore the info from peer
                    uint32_t send_index = ack_ctl_info->big.send_index;
                    isend_info *isend_ptr = my_conn_p2p->isend_info_pool.get(send_index);
                    oneside_info* peer_info = (oneside_info*)isend_ptr->req_handle->oneside_info_addr;
                    ASSERT(peer_info);

                    peer_info->set_oneside_info(ack_ctl_info->big.recv_buffer, ack_ctl_info->big.rkey,
                                                ack_ctl_info->big.send_mr, ack_ctl_info->big.index,
                                                ack_ctl_info->big.send_index, ONE_SIDE_SEND);
                    isend_ptr->req_handle->_lock.release();
                    //memset(ack_ctl_info, 0, sizeof(ctl_flow_info));
                    my_conn_p2p->ctl_flow_pool.push(ack_ctl_info);
                    continue;

                }
                addr_mr_pair *mr_pair = my_conn_p2p->addr_mr_pool.pop();//remember to recycle
                ASSERT(mr_pair);
                mr_pair->send_addr = (uintptr_t)ack_ctl_info->big.send_mr->addr;
                mr_pair->send_mr = ack_ctl_info->big.send_mr;
                mr_pair->len = ack_ctl_info->big.send_mr->length;
                mr_pair->isend_index = ack_ctl_info->big.send_index;
                uint32_t imm_data = ack_ctl_info->big.index;
                imm_data |= IMM_DATA_MAX_MASK;
                //ERROR("imm_data: %d %d\n", imm_data, ack_ctl_info->big.index);

                my_conn_p2p->_lock_for_peer_num.acquire();
                if(my_conn_p2p->peer_left_recv_num <= 0){
                    //WARN("===============\n");
                    my_conn_p2p->unsend_queue.emplace(mr_pair, ack_ctl_info->big.recv_buffer,
                                         ack_ctl_info->big.rkey, imm_data);
                    my_conn_p2p->_lock_for_peer_num.release();
                }
                else{
                    my_conn_p2p->peer_left_recv_num--;
                    my_conn_p2p->_lock_for_peer_num.release();
                    CCALL(my_conn_p2p->pp_post_write(mr_pair, ack_ctl_info->big.recv_buffer, ack_ctl_info->big.rkey, imm_data));
                    ITR_POLL("sending real big msg: recv_buffer %llx, rkey %x, len %d\n",
                             (long long)ack_ctl_info->big.recv_buffer, ack_ctl_info->big.rkey, mr_pair->len);
                }
                //push the ack_ctl_info
                //memset(ack_ctl_info, 0, sizeof(ctl_flow_info));
                my_conn_p2p->ctl_flow_pool.push(ack_ctl_info);
            }
        }
        else if(op == IBV_WC_RDMA_WRITE){
            //means small msg or big msg have sent
            addr_mr_pair *mr_pair = (addr_mr_pair*)(wc->wr_id);
            int isend_index = mr_pair->isend_index;
            //WARN("========= isend_index %d\n", isend_index);
            //memset(mr_pair, 0, sizeof(addr_mr_pair));
            rdma_conn_p2p* my_conn_p2p = (rdma_conn_p2p*)mr_pair->which_qp->qp_context;
            my_conn_p2p->addr_mr_pool.push(mr_pair);
            my_conn_p2p->isend_info_pool.get(isend_index)->req_handle->_lock.release();
        }else{
            ERROR("unknown type when do_recv_completion");
            return false;
        }
    }
    return true;
}


bool conn_system::do_recv_completion(int n, struct ibv_wc *wc_recv){
    for(int i = 0;i < n;i++){
        struct ibv_wc *wc = wc_recv+i;
        if(wc->status != IBV_WC_SUCCESS){
            ERROR("some error when do_recv_completion (%s).\n",ibv_wc_status_str(wc->status));
            return false;
        }
        enum ibv_wc_opcode op = wc->opcode;
        if(op == IBV_WC_SEND)
            continue;
        enum RECV_TYPE type;
        int index = -1;
        rdma_conn_p2p* my_conn_p2p = nullptr;
        mr_pair_recv *tmp_id = (mr_pair_recv*)(wc->wr_id);
        struct ibv_qp *my_qp = tmp_id->which_qp;
        struct ibv_mr *recv_mr = tmp_id->recv_mr;
        my_conn_p2p = (rdma_conn_p2p*)my_qp->qp_context;
        my_conn_p2p->recv_mr_pool.push(tmp_id);

        if(op == IBV_WC_RECV_RDMA_WITH_IMM){
            uint32_t imm_data = ntohl(wc->imm_data);
            int type_bit = imm_data >> 31;
            if(type_bit){//RECV BIG_MSG
                index = imm_data & IMM_DATA_SMALL_MASK;
                type  = BIG_WRITE_IMM;
            }
            else{
                index = imm_data; //small_msg_size
                type = SMALL_WRITE_IMM;
            }

        }
        else if(op == IBV_WC_RECV){
            type = SEND_REQ_MSG;
        }
        else{
            ERROR("capture some unknown op when do_recv_completion.\n");
            return false;
        }
        //the _lock need to be reconsidered
        my_conn_p2p->_lock.acquire();
        my_conn_p2p->used_recv_num++;
        my_conn_p2p->_lock.release();
        if(type == BIG_WRITE_IMM){
            irecv_info *big_msg_ptr = my_conn_p2p->irecv_info_pool.get(index);
            ASSERT(big_msg_ptr);
            //ITR_SPECIAL("big_msg_ptr index:%d, big_msg_ptr_addr:%llx, recv_addr %llx\n", big_msg_ptr->req_handle->index, (long long)big_msg_ptr, (long long)big_msg_ptr->recv_addr);
            ASSERT(big_msg_ptr->recv_mr);
            big_msg_ptr->req_handle->hp_cur_times++;
            big_msg_ptr->req_handle->_lock.release();
        }
        else{
            if(my_conn_p2p->irecv_queue.empty()){
                //double check
                my_conn_p2p->_lock.acquire();
                if(my_conn_p2p->irecv_queue.empty()){
                    if(type == SEND_REQ_MSG){
                        //push the big_msg_req into pending_queue
                        send_req_clt_info *recvd_req = (send_req_clt_info*)recv_mr->addr;
                        pending_send pending_req;
                        pending_req.is_big = true;
                        ASSERT(recvd_req->send_mr);
                        pending_req.big.big_addr = (uintptr_t)recvd_req->send_mr->addr;
                        pending_req.big.big_mr   = recvd_req->send_mr;
                        pending_req.big.size     = recvd_req->send_mr->length;
                        pending_req.big.isend_index = recvd_req->isend_index;
                        pending_req.big.is_oneside  = recvd_req->is_oneside;
                        my_conn_p2p->pending_queue.push(pending_req);
                    } else{
                        //push the small_msg into pending_queue
                        ITR_POLL("irecv_queue is empty, Receiving.............\n");
                        pending_send pending_req;
                        pending_req.is_big = false;
                        pending_req.small.size = index;//index means the size of the small msg
                        pending_req.small.pos  = (char*)my_conn_p2p->recv_local_buf_status.buf_info.addr
                                                 + my_conn_p2p->recv_local_buf_status.pos_isend;
                        my_conn_p2p->recv_local_buf_status.pos_isend = (my_conn_p2p->recv_local_buf_status.pos_isend + index)
                                                                       % my_conn_p2p->recv_local_buf_status.buf_info.size;
                        my_conn_p2p->pending_queue.push(pending_req);
                    }
                }
                else{
                    my_conn_p2p->_lock.release();
                    my_conn_p2p->irecv_queue_not_empty(type, recv_mr , index);
                    if(my_conn_p2p->recvd_bufsize >= THREHOLD_RECVD_BUFSIZE || my_conn_p2p->used_recv_num >= MAX_POST_RECV_NUM){
                        ITR_SPECIAL("### (in poll)used_recv_num (%d), recvd_bufsize (%lld), feedback the situation to sender.###\n",
                                    my_conn_p2p->used_recv_num, (long long)my_conn_p2p->recvd_bufsize);
                        my_conn_p2p->reload_post_recv();
                    }
                    return 1;
                }
                my_conn_p2p->_lock.release();
            }
            else{//irecv_queue is not empty
                my_conn_p2p->irecv_queue_not_empty(type, recv_mr , index);
            }
        }
        if(my_conn_p2p->recvd_bufsize >= THREHOLD_RECVD_BUFSIZE || my_conn_p2p->used_recv_num >= MAX_POST_RECV_NUM){
            ITR_SPECIAL("### (in poll)used_recv_num (%d), recvd_bufsize (%lld), feedback the situation to sender.###\n",
                        my_conn_p2p->used_recv_num, (long long)my_conn_p2p->recvd_bufsize);
            my_conn_p2p->reload_post_recv();
        }
        //_lock.release();
    }
    return true;
}

#endif

