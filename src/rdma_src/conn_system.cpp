#include "conn_system.h"
#include <functional>

conn_system::~conn_system() {
    WARN("Transfer System is ready to close.\n");
    env.dispose();
    connecting_map.Foreach([](std::string key, rdma_conn_p2p * conn){
        conn->isruning = false;
        conn->poll_thread->join();
    });
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
    this->my_listen_ip = (char*)malloc(strlen(ip)+1);
    strcpy(this->my_listen_ip, ip);
    this->my_listen_port = port;

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
    //usleep(100);
}

rdma_conn_p2p* conn_system::init(char* peer_ip, int peer_port)
{
    //judge whether the rdma_conn_p2p *ret has already existed
    std::string key = std::string(peer_ip) + "_" + std::to_string(peer_port);
    rdma_conn_p2p *conn_object = nullptr;
    if(connecting_map.InContains(key)){
        ASSERT(connecting_map.Get(key, &conn_object));
        ASSERT(conn_object);
    }
    else{
        _lock.acquire();
        if(!connecting_map.InContains(key)){
            conn_object = new rdma_conn_p2p();
            connecting_map.Set(key, conn_object);
        }
        else{
            ASSERT(connecting_map.Get(key, &conn_object));
        }
        _lock.release();
    }
    ASSERT(conn_object);

    socket_connection *send_conn = env.create_connection(peer_ip, peer_port);
    set_active_connection_callback(send_conn, key);
    send_conn->async_connect();
    IDEBUG("Ready to establish with %s\n", key.c_str());

    uint64_t dummy;
    CCALL(read(conn_object->send_event_fd, &dummy, sizeof(dummy)));
    close(conn_object->send_event_fd);
    //fill the conn_object for send_direction
    exchange_qp_data send_direction_data = conn_object->send_direction_qp;
    SUCC("[%s] SEND_direction my_qp_info:   LID 0x%04x, QPN 0x%06x\n", key.c_str(),
         conn_object->send_rdma_conn.portinfo.lid, conn_object->send_rdma_conn.qp->qp_num);
    SUCC("[%s] SEND_direction peer_qp_info: LID 0x%04x, QPN 0x%06x\n", key.c_str(),
         send_direction_data.lid, send_direction_data.qpn);
    recvd_buf_info &tmp_send_info = conn_object->send_peer_buf_status.buf_info;
    SUCC("[%s] SEND_direction send_peer_buf_status: addr 0x%llx, rkey 0x%llx, size 0x%llx, mr 0x%llx\n", key.c_str(),
         (long long)tmp_send_info.addr, (long long)tmp_send_info.rkey, (long long)tmp_send_info.size, (long long)tmp_send_info.buff_mr);


    CCALL(read(conn_object->recv_event_fd, &dummy, sizeof(dummy)));
    close(conn_object->recv_event_fd);
    exchange_qp_data recv_direction_data = conn_object->recv_direction_qp;

    SUCC("[%s] RECV_direction my_qp_info:   LID 0x%04x, QPN 0x%06x\n", key.c_str(),
         conn_object->recv_rdma_conn.portinfo.lid, conn_object->recv_rdma_conn.qp->qp_num);
    SUCC("[%s] RECV_direction peer_qp_info: LID 0x%04x, QPN 0x%06x\n", key.c_str(),
         recv_direction_data.lid, recv_direction_data.qpn);
    recvd_buf_info &tmp_recv_info = conn_object->recv_local_buf_status.buf_info;
    SUCC("[%s] RECV_direction recv_local_buf_status: addr 0x%llx, rkey 0x%llx, size 0x%llx, mr 0x%llx\n", key.c_str(),
         (long long)tmp_recv_info.addr, (long long)tmp_recv_info.rkey, (long long)tmp_recv_info.size, (long long)tmp_recv_info.buff_mr);

    SUCC("[===== %s_%d FINISH INIT TO %s.=====]\n", my_listen_ip, my_listen_port, key.c_str());
    //close used fd
    conn_object->clean_used_fd();
    run_poll_thread(conn_object);
    //ERROR("===================\n");
    return conn_object;
}

void conn_system::run_poll_thread(rdma_conn_p2p* conn_object){
    conn_object->poll_thread = new std::thread(std::bind(&rdma_conn_p2p::poll_func, conn_object, conn_object));
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
        usleep(50000);
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
            pending_conn->nofity_system(pending_conn->send_event_fd);
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
                //IDEBUG("recv_conn:key(%s)\n",peer_key.c_str());

            rdma_conn_p2p *conn_object = nullptr;
            ASSERT(connecting_map.Get(peer_key, &conn_object));
            conn_object->nofity_system(conn_object->recv_event_fd);
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
                //IDEBUG("recv_conn:key(%s)\n",peer_key.c_str());

                rdma_conn_p2p *conn_object = nullptr;
                if(connecting_map.InContains(peer_key)){
                    ASSERT(connecting_map.Get(peer_key, &conn_object));
                    ASSERT(conn_object);
                }
                else{
                    _lock.acquire();
                    if(!connecting_map.InContains(peer_key)){
                        conn_object = new rdma_conn_p2p();
                        connecting_map.Set(peer_key, conn_object);
                    }
                    else{
                        ASSERT(connecting_map.Get(peer_key, &conn_object));
                    }
                    _lock.release();
                }
                ASSERT(conn_object);
                conn_object->recv_direction_qp.lid = ntohs(conn->cur_recv_info.qp_all_info.qp_info.lid);
                conn_object->recv_direction_qp.qpn = ntohl(conn->cur_recv_info.qp_all_info.qp_info.qpn);

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

