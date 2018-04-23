#include "tcp_conn_system.h"
#include "tcp_resource.h"
#define IP_LEN 16
void tcp_conn_system::splitkey(const std::string& s, std::string& ip, int &port, const std::string& c)
{
    std::string::size_type pos1, pos2;
    pos2 = s.find(c); pos1 = 0;
    ip = s.substr(pos1, pos2-pos1);
    pos1 = pos2 + c.size();
    port = atoi(s.substr(pos1).c_str());
}
tcp_conn_system::tcp_conn_system(const char *ip, int port)
{
    IDEBUG("SYSTEM choose tcp_conn_system class.\n");
    this->my_listen_ip = (char*)malloc(IP_LEN);
    strcpy(this->my_listen_ip, ip);
    this->my_listen_port = port;

    lis = env.create_listener(my_listen_ip, my_listen_port);
    lis->OnAccept = [&](listener*,  connection* conn){
        set_passive_connection_callback(conn);
        conn->start_receive();
        IDEBUG("[TCP_conn_system OnAccept] \n");
    };
    lis->OnAcceptError = [&](listener*, const int error){
        ERROR("[TCP_conn_system %s:%d] OnAcceptError......\n", ip, port);
    };
    lis->OnClose = [&](listener*){
        DEBUG("[TCP_conn_system] You can deal with it on close.\n");
    };
    bool success = lis->start_accept();
    ASSERT(success);
}

async_conn_p2p* tcp_conn_system::init(char* peer_ip, int peer_port){
    std::string key = std::string(peer_ip) + "_" +std::to_string(peer_port);
    tcp_conn_p2p *conn_object = nullptr;
    if(connecting_map.InContains(key)){
        ASSERT(connecting_map.Get(key, &conn_object));
        ASSERT(conn_object);
    }
    else{
        _lock.acquire();
        if(!connecting_map.InContains(key)){
            conn_object = new tcp_conn_p2p();
            connecting_map.Set(key, conn_object);
        }
        else{
            ASSERT(connecting_map.Get(key, &conn_object));
        }
        _lock.release();
    }
    ASSERT(conn_object);
    memcpy(conn_object->my_listen_ip, this->my_listen_ip, strlen(this->my_listen_ip)+1);
    conn_object->my_listen_port = my_listen_port;
    ITR_SPECIAL("conn_object's my_listen_ip:%s, my_listen_port:%d\n",
                conn_object->my_listen_ip, conn_object->my_listen_port);
    socket_connection *send_conn = env.create_connection(peer_ip, peer_port);
    set_active_connection_callback(send_conn, key);
    send_conn->async_connect();
    IDEBUG("Ready to establish with %s\n", key.c_str());
    conn_object->_active_connected.acquire();
    conn_object->_passive_connected.acquire();
    SUCC("[===== %s_%d TCP_CONN FINISH INIT TO %s.=====]\n", my_listen_ip, my_listen_port, key.c_str());
    return conn_object;
}

void tcp_conn_system::set_active_connection_callback(connection *send_conn, std::string key){
    send_conn->OnConnect = [key, this](connection* conn){
        tcp_conn_p2p *key_object = nullptr;
        ASSERT(connecting_map.Get(key, &key_object));
        key_object->send_socket_conn = (socket_connection*)conn;
        datahead *head = conn->datahead_pool.pop();
        head->type = HEAD_TYPE_INIT;
        head->content_size = 0;
        memcpy(head->ip, key_object->my_listen_ip, strlen(key_object->my_listen_ip)+1);

        head->port = key_object->my_listen_port;
        ASSERT(conn->async_send(head, sizeof(datahead)));
        WARN("READY to send my_listen_ip (%s) and my_listen_port (%d) info to peer.\n", head->ip, head->port);
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

    send_conn->OnClose = [key, this](connection* conn){
        if(((socket_connection*)conn)->get_conn_status() == CONNECTION_CONNECT_FAILED)
            DEBUG("the failed connection close %s.\n", key.c_str());
        else
            DEBUG("send_conn close normally.\n");
    };

    send_conn->OnHup = [key, this](connection* conn, const int error){
        if (error == 0) {
            TRACE("[OnHup] Because rank %s is close normally...\n", key.c_str());
        }
        else {
            ERROR("[OnHup] Because rank %s is close Abnormal...\n", key.c_str());
        }
    };

    send_conn->OnSend = [key, this](connection* conn, const void* buffer, const size_t length){
        if(conn->is_sent_head){
            ASSERT(length == sizeof(datahead));
            datahead* head = (datahead*)const_cast<void*>(buffer);
            if(head->content_size == 0){
                ASSERT(head->type == HEAD_TYPE_INIT);
                conn->datahead_pool.push(head);
                tcp_conn_p2p *pending_conn;
                connecting_map.Get(key, &pending_conn); ASSERT(pending_conn);
                pending_conn->_active_connected.release();
            }
            else{
                ASSERT(length == sizeof(datahead));
                conn->datahead_pool.push((datahead*)const_cast<void*>(buffer));
                conn->is_sent_head = false;//means next onsend buffer is the real send content
            }
        }
        else{
            non_block_handle *send_handler;
            bool success = conn->sending_data_queue.try_pop(&send_handler);
            ASSERT(success);
            ASSERT(send_handler);
            conn->is_sent_head = true;
            send_handler->_lock.release();
        }
    };
    send_conn->OnSendError = [key, this](connection* conn, const void* buffer, const size_t length,
                                         const size_t sent_length, const int error){
        ERROR("[%s] OnSendError: %d (%s). all %lld, sent %lld\n",
              key.c_str(), error, strerror(error), (long long)length, (long long)sent_length);
        ASSERT(0);

    };

}

void tcp_conn_system::set_passive_connection_callback(connection *recv_conn){
    recv_conn->OnClose = [this](connection* conn){
        DEBUG("[OnClose] recv_conn close normally.\n");
    };

    recv_conn->OnHup = [this](connection*, const int error){
        if (error == 0) TRACE("[OnHup] recv_conn close normally.\n");
        else ERROR("[OnHup] recv_conn close Abnormal...\n");
    };

    recv_conn->OnReceive = [this](connection* conn, const void* buffer, const size_t length) {

        size_t left_len = length;
        char* cur_buf   = (char*) const_cast<void*>(buffer);
        //this part can be made a flow or chart
        while(left_len > 0){
            size_t &rhs = conn->cur_recv_data.recvd_head_size;//rhs == recvd_head_size
            if(rhs < sizeof(datahead)){
                if(left_len < sizeof(datahead) - rhs){
                    memcpy(&(conn->cur_recv_data.head_msg)+rhs, cur_buf, left_len);
                    rhs += left_len;
                    break;
                }
                else{
                    memcpy(&(conn->cur_recv_data.head_msg)+rhs, cur_buf, sizeof(datahead) - rhs);
                    cur_buf  += (sizeof(datahead) - rhs);
                    left_len -= (sizeof(datahead) - rhs);
                    rhs = sizeof(datahead);
                    if(conn->cur_recv_data.head_msg.type == HEAD_TYPE_SEND){
                        size_t cts = conn->cur_recv_data.head_msg.content_size;
                        conn->cur_recv_data.total_content_size = cts;
                        conn->cur_recv_data.content = (char*)malloc(cts);
                    }
                    else if(conn->cur_recv_data.head_msg.type == HEAD_TYPE_INIT){
                        std::string peer_key = std::string(conn->cur_recv_data.head_msg.ip) + "_" +
                                               std::to_string(conn->cur_recv_data.head_msg.port);
                        //IDEBUG("recv_conn:peer_key (%s)\n",peer_key.c_str());

                        tcp_conn_p2p *conn_object = nullptr;
                        if(connecting_map.InContains(peer_key)){
                            ASSERT(connecting_map.Get(peer_key, &conn_object));
                            ASSERT(conn_object);
                        }
                        else{
                            _lock.acquire();
                            if(!connecting_map.InContains(peer_key)){
                                conn_object = new tcp_conn_p2p();
                                connecting_map.Set(peer_key, conn_object);
                            }
                            else{
                                ASSERT(connecting_map.Get(peer_key, &conn_object));
                            }
                            _lock.release();
                        }
                        ASSERT(conn_object);

                        conn_object->recv_socket_conn = (socket_connection*)conn;
                        memcpy(conn_object->peer_listen_ip, conn->cur_recv_data.head_msg.ip,
                               strlen(conn->cur_recv_data.head_msg.ip)+1);
                        conn_object->peer_listen_port = conn->cur_recv_data.head_msg.port;
                        ASSERT(conn->cur_recv_data.head_msg.content_size == 0);
                        ITRACE("%s_%dHas Recvd the HEAD_TYPE_INIT from %s\n",
                               this->my_listen_ip, this->my_listen_port, peer_key.c_str());
                        conn->cur_recv_data.clear();
                        conn_object->_passive_connected.release();
                    }
                    continue;
                }
            }
            else{
                size_t &rcs = conn->cur_recv_data.recvd_content_size;
                size_t &tcs = conn->cur_recv_data.total_content_size;
                if(left_len < tcs - rcs){
                    memcpy(conn->cur_recv_data.content + rcs, cur_buf, left_len);
                    rcs += left_len;
                    break;
                }
                else{
                    memcpy(conn->cur_recv_data.content + rcs, cur_buf, tcs - rcs);
                    cur_buf  += (tcs - rcs);
                    left_len -= (tcs - rcs);
                    rcs = conn->cur_recv_data.total_content_size;
                    conn->recvd_data_queue.push(conn->cur_recv_data);
                    ITR_RECV("[On receive] push one whole data into queue.\n");
                    conn->cur_recv_data.clear();
                    continue;
                }
            }
        }
    };
}
