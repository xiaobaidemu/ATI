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

struct ctl_data{
    enum head_type type;
    char ip[16];
    int  port;
    exchange_qp_data qp_info;
}__attribute__((packed));

struct exch_state{
    size_t recvd_size;
    struct ctl_data qp_all_info;
public:
    exch_state():recvd_size(0){}
};


typedef struct exchange_qp_data       exchange_qp_data;
typedef struct unidirection_rdma_conn unidirection_rdma_conn;
typedef struct ctl_data               ctl_data;

#endif //SENDRECV_RDMA_RESOURCE_H
