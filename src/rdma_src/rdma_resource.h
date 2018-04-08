#ifndef SENDRECV_RDMA_RESOURCE_H
#define SENDRECV_RDMA_RESOURCE_H


#include <cstdint>
#include <infiniband/verbs.h>
#include <common/lock.h>

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
public:
    status_recv_buf(){
        pos_irecv = 0;
        pos_isend = 0;
    }
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
            uintptr_t big_addr;
            struct ibv_mr *big_mr;
            int    isend_index;
        }big;
        struct {
            void   *pos;
            size_t size;
        }small;
    };
};

struct ctl_flow_info{ //irecver with send these info to isender, isender will post_recv these info
    int type;// 0 means ctl, 1 means big
    union{
        struct{
            int used_recv_num;//related with post_recv
            int recvd_bufsize;
        }ctl;
        struct{
            uintptr_t recv_buffer;
            uint32_t rkey;
            uintptr_t send_buffer;
            struct ibv_mr *send_mr;
            int  index;      //use for irecv_info_pool
            int  send_index; //use for isend_info_pool
        }big;
    };
};


struct addr_mr_pair{
    uintptr_t     send_addr;
    struct ibv_mr *send_mr;
    uint32_t      len;
    int           isend_index;
};

struct send_req_clt_info{
    uintptr_t     send_addr;
    struct ibv_mr *send_mr;
    uint32_t      len;
    int           isend_index;
};

/*struct send_ack_clt_info{
    uintptr_t recv_addr;
    uint32_t  rkey;
    uintptr_t send_addr;
    struct ibv_mr *send_mr;
    int       index;
};*/
enum RECV_TYPE{
    BIG_WRITE_IMM = 0,
    SMALL_WRITE_IMM,
    SEND_REQ_MSG,
};

enum WAIT_TYPE{
    WAIT_ISEND = 0,
    WAIT_IRECV,
};
struct non_block_handle{
    lock _lock;
    int  index;//related to arraypool,array
    enum WAIT_TYPE type;
};

struct irecv_info{
    uintptr_t     recv_addr;
    size_t        recv_size;
    struct ibv_mr *recv_mr;
    non_block_handle *req_handle;
};

struct isend_info{
    uintptr_t     send_addr;
    size_t        send_size;
    struct ibv_mr *send_mr;
    non_block_handle *req_handle;
};

struct unsend_element{
    bool is_real_msg;
    union{
        struct {
            addr_mr_pair *mr_pair;
            uint64_t remote_addr;
            uint32_t rkey;
            uint32_t imm_data;
        }real_msg_info;
        struct {
            send_req_clt_info req_msg;
        }req_msg_info;
    };
public:
    unsend_element(){}
    unsend_element(send_req_clt_info info){
        is_real_msg = false;
        req_msg_info.req_msg = info;
    }
    unsend_element(addr_mr_pair *mr_pair, uint64_t remote_addr, uint32_t rkey, uint32_t imm_data){
        is_real_msg = true;
        real_msg_info.mr_pair = mr_pair;
        real_msg_info.remote_addr = remote_addr;
        real_msg_info.rkey = rkey;
        real_msg_info.imm_data = imm_data;
    }
};



typedef struct exchange_qp_data       exchange_qp_data;
typedef struct unidirection_rdma_conn unidirection_rdma_conn;
typedef struct ctl_data               ctl_data;
typedef struct pending_send           pending_send;
typedef struct recvd_buf_size         recvd_buf_size;
typedef struct status_recv_buf        status_recv_buf;
typedef struct ctl_flow_info          ctl_flow_info;
typedef struct send_req_clt_info      send_req_clt_info;
typedef struct non_block_handle       non_block_handle;
typedef struct addr_mr_pair           addr_mr_pair;
typedef struct irecv_info             irecv_info;
typedef struct unsend_element         unsend_element;
//typedef struct send_ack_clt_info      send_ack_clt_info;
#endif //SENDRECV_RDMA_RESOURCE_H
