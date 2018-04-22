#ifndef SENDRECV_SYSTEM_H
#define SENDRECV_SYSTEM_H
#include<iostream>
#include <common/common.h>
#include <sendrecv.h>
#include <string>
#include <common/safemap.h>
#include <endian.h>
#include <byteswap.h>
#include "rdma_conn_p2p.h"

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

class rdma_conn_p2p;

typedef safemap<std::string, rdma_conn_p2p*> CONN_MAP;

class conn_system {
public:
    conn_system(const conn_system&) = delete;
    conn_system(conn_system && ) = delete;
    conn_system & operator=(const conn_system&) = delete;
    ~conn_system();

private:
    lock _lock;//use for double check
    socket_environment env;
    socket_listener   *lis;
    char* my_listen_ip;
    int   my_listen_port;

    CONN_MAP connecting_map;

public:
    conn_system(const char* my_listen_ip, int my_listen_port);
    rdma_conn_p2p* init(char* peer_ip, int peer_port);
    void conn_system_finalize(){
        env.dispose();
    }

private:
    void set_active_connection_callback(connection * conn, std::string ip_port_key);
    void set_passive_connection_callback(connection * conn);
    void splitkey(const std::string& s, std::string& ip, int &port, const std::string& c);
    void run_poll_thread(rdma_conn_p2p* conn_object);
};

#include "rdma_conn_p2p.h"
#endif //SENDRECV_SYSTEM_H
