//at_sendrecv means: asynchronous communication isend and irecv
//this file is used for
#ifndef SENDRECV_AT_SENDRECV_H
#define SENDRECV_AT_SENDRECV_H

#include <common/common.h>
#include <rdma_src/rdma_resource.h>
#define IP_LEN 16

enum SYS_TYPE{
    SYSTEM_INVALID = 0,
    TCP_SYSTEM,
    RDMA_SYSTEM,
};

class async_conn_p2p;
class async_conn_system;
class comm_system{
private:
    char  my_ip[IP_LEN];
    int   my_port;
public:
    comm_system(const comm_system&) = delete;
    comm_system(comm_system &&) = delete;
    comm_system &operator=(const comm_system&) = delete;
    comm_system(const char* my_listen_ip, int my_listen_port){
        strcpy(this->my_ip, my_listen_ip);
        this->my_port = my_listen_port;
    }
    async_conn_system* get_conn_system();
    bool use_oneside(){
        return conn_sys_type == RDMA_SYSTEM ? true : false;
    }
private:
    enum SYS_TYPE conn_sys_type;
    bool verifyrdma();
};

//conn_system & rdma_conn_system inhert async_conn_system
class async_conn_system
{
public:
    async_conn_system(const async_conn_system&) = delete;
    async_conn_system(async_conn_system &&) = delete;
    async_conn_system &operator=(const async_conn_system&) = delete;
    ~async_conn_system(){}

protected:
    async_conn_system(){}

public:
    virtual async_conn_p2p* init(char* peer_ip, int peer_port) = 0;
};

//rdma_conn_p2p &tcp_conn_p2p inherit async_conn_p2p
class async_conn_p2p
{
protected:
    async_conn_p2p(){};

public:
    async_conn_p2p(const async_conn_p2p&) = delete;
    async_conn_p2p(async_conn_p2p &&) = delete;
    async_conn_p2p & operator=(const async_conn_p2p&) = delete;

    virtual int isend(const void *buf, size_t count, non_block_handle *req) = 0;
    virtual int irecv(void *buf, size_t count, non_block_handle *req) = 0;
    virtual bool wait(non_block_handle* req) = 0;
    virtual int oneside_send_pre(const void *buf, size_t count, non_block_handle *req, oneside_info *peer_info) = 0;
    virtual int oneside_recv_pre(void *buf, size_t count, non_block_handle *req, oneside_info* my_info) = 0;
    virtual bool end_oneside(oneside_info *peer_info) = 0;

    virtual int oneside_isend(oneside_info *peer_info, non_block_handle *req) = 0;
};



#endif //SENDRECV_AT_SENDRECV_H
