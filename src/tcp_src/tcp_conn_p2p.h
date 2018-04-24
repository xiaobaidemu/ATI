//
// Created by heboxin on 18-4-15.
//

#ifndef SENDRECV_TCP_CONN_P2P_H
#define SENDRECV_TCP_CONN_P2P_H

#include <queue>
#include <sendrecv.h>
#include "tcp_conn_system.h"
#include "tcp_resource.h"
#include <at_sendrecv.h>


class tcp_conn_p2p : public async_conn_p2p
{
    friend class tcp_conn_system;
private:
    lock _active_connected;
    lock _passive_connected;
    socket_connection *send_socket_conn;
    socket_connection *recv_socket_conn;

    char my_listen_ip[16];
    int  my_listen_port;
    char peer_listen_ip[16];
    int  peer_listen_port;


public:
    tcp_conn_p2p(const tcp_conn_p2p&) = delete;
    tcp_conn_p2p(tcp_conn_p2p && ) = delete;
    tcp_conn_p2p & operator=(const tcp_conn_p2p&) = delete;

    tcp_conn_p2p();

    int isend(const void *buf, size_t count, non_block_handle *req);
    int irecv(void *buf, size_t count, non_block_handle *req);
    bool wait(non_block_handle* req);

    int oneside_send_pre(const void *buf, size_t count, non_block_handle *req, oneside_info *peer_info) {
        return 0;
    }
    int oneside_recv_pre(void *buf, size_t count, non_block_handle *req, oneside_info* my_info){
        return 0;
    }
    bool end_oneside(oneside_info *peer_info){
        return false;
    }

    int oneside_isend(oneside_info *peer_info, non_block_handle *req){
        return 0;
    }
};


#endif //SENDRECV_TCP_CONN_P2P_H
