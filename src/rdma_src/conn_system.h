#ifndef SENDRECV_SYSTEM_H
#define SENDRECV_SYSTEM_H
#include<iostream>
#include <common/common.h>
#include <sendrecv.h>
#include <string>
#include <common/safemap.h>

class rdma_conn_p2p;

typedef safemap<std::string, rdma_conn_p2p*> CONN_MAP;

class conn_system {
public:
    conn_system(const conn_system&) = delete;
    conn_system(conn_system && ) = delete;
    conn_system & operator=(const conn_system&) = delete;
    ~conn_system() {
        WARN("Transfer System is ready to close.\n");
        env.dispose();
    }

private:
    lock _lock;//use for double check
    socket_environment env;
    socket_listener   *lis;
    char* my_listen_ip;
    int   my_listen_port;

    CONN_MAP connecting_map;
    //SUCC_CONN_MAP success_connect_map; //those have already established connect
    //PENDING_CONN_MAP passive_connect_map;
    //PENDING_CONN_MAP active_connect_map;

public:
    conn_system(const char* my_listen_ip, int my_listen_port);
    rdma_conn_p2p* init(char* peer_ip, int peer_port);
    void conn_system_finalize(){
        //env.dispose();
    }

private:
    void set_active_connection_callback(connection * conn, std::string ip_port_key);
    void set_passive_connection_callback(connection * conn);
    void splitkey(const std::string& s, std::string& ip, int &port, const std::string& c);
};

#include "rdma_conn_p2p.h"
#endif //SENDRECV_SYSTEM_H
