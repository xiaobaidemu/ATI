#ifndef SENDRECV_RDMA_RESOURCE_H
#define SENDRECV_RDMA_RESOURCE_H
#include <common/lock.h>
#include <cstdint>
enum WAIT_TYPE{
    WAIT_ISEND = 0,
    WAIT_IRECV,
    WAIT_ONESIDE_SEND_PRE,
    WAIT_ONESIDE_RECV_PRE,
    WAIT_ONESIDE_SEND,
    WAIT_ONESIDE_RECV,
    WAIT_HP_ISEND_INIT,
    WAIT_HP_IRECV_INIT,
    WAIT_HP_ISEND,

};

struct non_block_handle{
    lock _lock;
    int  index;//related to arraypool,array
    enum WAIT_TYPE type;
    uintptr_t oneside_info_addr;//only for oneside_send_pre/hp_isend_pre
    //only for tcp_conn_system
    struct{
        void   *tcp_irecv_addr;
        size_t real_recv_size;
    }tcp_req_info;
    int hp_irecv_times;//the max times receive side can receive
    volatile int hp_cur_times;//the times receive side has received
};


typedef struct non_block_handle       non_block_handle;

#ifdef IBEXIST

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
    TCP_HEAD_TYPE_INVAILD = 0,
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




struct addr_mr_pair{
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



enum ONE_SIDE_TYPE{
    ONE_SIDE_NONE = 0,
    ONE_SIDE_SEND,
    ONE_SIDE_RECV,
};

struct send_req_clt_info{
    uintptr_t     send_addr;
    struct ibv_mr *send_mr;
    uint32_t      len;
    int           isend_index;
    bool          is_oneside;
    bool          is_hp_init;
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
            bool is_oneside;
        }big;
    };
};

struct pending_send{
    bool is_big;//true : big msg_request
    union{
        struct{
            size_t size;
            uintptr_t big_addr;
            struct ibv_mr *big_mr;
            int    isend_index;
            bool   is_oneside;
            bool   is_hp_init;
        }big;
        struct {
            void   *pos;
            size_t size;
        }small;
    };
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

struct hp_oneside_info{
    uintptr_t recv_buffer;
    uint32_t  rkey;
    int       read_index_start;
    int       total_send_times;
};

struct oneside_info{
    uintptr_t recv_buffer;
    uint32_t  rkey;
    uintptr_t send_buffer;
    struct ibv_mr *send_mr;
    int  read_index;    //use for irecv_info_pool
    int  write_index; //use for isend_info_pool
    enum ONE_SIDE_TYPE type;
    //for recv part
    non_block_handle *req;
public:
    oneside_info(){}
    oneside_info(uintptr_t rb, uint32_t rk, uintptr_t sb, struct ibv_mr *sm, int ri, int wi):
            recv_buffer(rb),rkey(rk), send_buffer(sb), send_mr(sm), read_index(ri), write_index(wi)
    {}
    void set_oneside_info(uintptr_t rb, uint32_t rk, uintptr_t sb, struct ibv_mr *sm, int ri, int wi, enum ONE_SIDE_TYPE t){
        recv_buffer = rb;
        rkey        = rk;
        send_buffer = sb;
        send_mr     = sm;
        read_index  = ri;
        write_index = wi;
        type        = t;
    }
};

struct hp_isend_info{
    uintptr_t recv_buffer;
    uint32_t  rkey;
    int total_put_times;
    int cur_put_times;
    non_block_handle *req;
public:
    hp_isend_info(){}
    void set_hp_isend_info(uintptr_t rb, uint32_t rk){
        recv_buffer = rb;
        rkey =rk;
        cur_put_times = 0;
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
typedef struct addr_mr_pair           addr_mr_pair;
typedef struct irecv_info             irecv_info;
typedef struct unsend_element         unsend_element;
typedef struct oneside_info           oneside_info;
//typedef struct send_ack_clt_info      send_ack_clt_info;
#endif //SENDRECV_RDMA_RESOURCE_H
#endif
