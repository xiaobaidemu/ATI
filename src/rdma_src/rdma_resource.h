#ifndef SENDRECV_RDMA_RESOURCE_H
#define SENDRECV_RDMA_RESOURCE_H


#include <cstdint>
#include <infiniband/verbs.h>

struct exchange_qp_data {
    uint16_t lid; //LID of IB port (Local IDentifier)
    uint32_t qpn; //qp number
    //int psn; //The Packet Serial Number makes sure that packets are being received by the order.
             // This helps detect missing packets and packet duplications
    //uint8_t gid[16] ;
} __attribute__((packed));

struct unidirection_rdma_conn{
    struct ibv_context	*context;
    struct ibv_comp_channel *channel;
    struct ibv_pd		*pd;
    struct ibv_cq		*cq;
    struct ibv_qp		*qp;
    int			        rx_depth;
    int                 ib_port;
    struct ibv_port_attr     portinfo;//
};

enum head_type{
    HEAD_TYPE_INVAILD = 0,
    HEAD_TYPE_EXCH,
    HEAD_TYPE_DONE,
};

struct recvd_buf_info{
    uint64_t addr;
    uint32_t rkey;
    size_t   size;
    uintptr_t  buff_mr;
};

//record the usage pf the recvd_buf
struct status_recv_buf{
    recvd_buf_info buf_info;
    size_t pos_irecv;
    size_t pos_isend;
};


struct ctl_data{
    enum head_type type;
    char ip[16];
    int  port;
    exchange_qp_data qp_info;
    recvd_buf_info   peer_buf_info;

}__attribute__((packed));


struct exch_state{
    size_t recvd_size;
    struct ctl_data qp_all_info;
public:
    exch_state():recvd_size(0){}
};

struct pending_send{
    bool is_big;//true : big msg_request
    union{
        struct{
            size_t size;
        }big;
        struct {
            void   *pos;
            size_t size;
        }small;
    };
};

struct ctl_flow_info{ //irecver with send these info to isender, isender will post_recv these info
    int type;
    union{
        struct{
            int used_recv_num;//related with post_recv
            int recvd_bufsize;
        }ctl;
        struct{
            void *recv_buffer;
            void *send_buffer;
            uint32_t rkey;
            int  index;
        }big;
    };
};

struct send_req_clt_info{
    size_t size;
};



typedef struct exchange_qp_data       exchange_qp_data;
typedef struct unidirection_rdma_conn unidirection_rdma_conn;
typedef struct ctl_data               ctl_data;
typedef struct pending_send           pending_send;
typedef struct recvd_buf_size         recvd_buf_size;
typedef struct status_recv_buf        status_recv_buf;
typedef struct ctl_flow_info          ctl_flow_info;
typedef struct send_req_clt_info      send_req_clt_info;

#endif //SENDRECV_RDMA_RESOURCE_H
