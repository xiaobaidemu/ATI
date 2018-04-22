//
// Created by heboxin on 18-4-15.
//

#ifndef SENDRECV_TCP_CONN_SYSTEM_H
#define SENDRECV_TCP_CONN_SYSTEM_H

#include<iostream>
#include <common/common.h>
#include <sendrecv.h>
#include <string>
#include <common/safemap.h>

class tcp_conn_p2p;

typedef safemap<std::string, tcp_conn_p2p*> TCP_CONN_MAP;

class tcp_conn_system {
public:
    tcp_conn_system(const tcp_conn_system&) = delete;
    tcp_conn_system(tcp_conn_system &&) = delete;
    tcp_conn_system &operator=(const tcp_conn_system&) = delete;
    ~tcp_conn_system(){

    }

private:
    lock _lock;
    socket_environment env;
    socket_listener   *lis;
    char* my_listen_ip;
    int   my_listen_port;
    TCP_CONN_MAP connecting_map;

public:
    tcp_conn_system(const char* my_listen_ip, int my_listen_port);
    tcp_conn_p2p* init(char* peer_ip, int peer_port);

private:
    void splitkey(const std::string& s, std::string& ip, int &port, const std::string& c);
    void set_passive_connection_callback(connection *recv_conn);
    void set_active_connection_callback(connection * conn, std::string ip_port_key);

};

#include "tcp_conn_p2p.h"
#endif //SENDRECV_TCP_CONN_SYSTEM_H
