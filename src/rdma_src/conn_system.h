#ifdef IBEXIST
#ifndef SENDRECV_SYSTEM_H
#define SENDRECV_SYSTEM_H
#define RX_DEPTH               (1024)
#include<iostream>
#include <common/common.h>
#include <sendrecv.h>
#include <string>
#include <common/safemap.h>
#include <endian.h>
#include <byteswap.h>
#include "rdma_conn_p2p.h"
#include <at_sendrecv.h>

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

class conn_system : public async_conn_system
{
    friend class rdma_conn_p2p;
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

    //context,pd,cq,complete_channel
    struct ibv_context	*context;
    struct ibv_comp_channel *channel;
    struct ibv_pd		*pd;
    struct ibv_cq		*cq;
    int			        rx_depth;
    int                 ib_port;
    struct ibv_port_attr     portinfo;

public:
    conn_system(const char* my_listen_ip, int my_listen_port);
    async_conn_p2p* init(char* peer_ip, int peer_port);
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
#endif
