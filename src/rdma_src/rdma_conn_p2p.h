#ifdef IBEXIST
#ifndef SENDRECV_RDMA_CONN_P2P_H
#define SENDRECV_RDMA_CONN_P2P_H

#include "rdma_resource.h"
#include <queue>
#include <sendrecv.h>
#include <sys/eventfd.h>
#include <at_sendrecv.h>
//#define RX_DEPTH               (1024)
#define MAX_INLINE_LEN         (128)
#define MAX_SGE_LEN            (1)
#define MAX_SMALLMSG_SIZE      (1024+1)
//#define MAX_SMALLMSG_SIZE      (1024*1024*512+1)
#define MAX_POST_RECV_NUM      (2048)
#define RECVD_BUF_SIZE         (1024*1024*2)
//#define RECVD_BUF_SIZE         (1024LL*1204*1024*2)
#define THREHOLD_RECVD_BUFSIZE (1024*1024)
//#define THREHOLD_RECVD_BUFSIZE (1024LL*1024*1024)
#define IMM_DATA_MAX_MASK      (0x80000000)
#define IMM_DATA_SMALL_MASK    (0x7fffffff)
class conn_system;

class rdma_conn_p2p : public async_conn_p2p
{
    friend class conn_system;
private:
    lock _lock_send_ech;
    lock _lock_recv_ech;
    conn_system *conn_sys;
private:
    socket_connection *send_socket_conn;
    socket_connection *recv_socket_conn;

    unidirection_rdma_conn send_rdma_conn;
    exchange_qp_data send_direction_qp;
    status_recv_buf  send_peer_buf_status;


    unidirection_rdma_conn recv_rdma_conn;
    exchange_qp_data recv_direction_qp;
    status_recv_buf  recv_local_buf_status;
    send_req_clt_info *post_array; //used for recv_qp recvd req_msg and small info
    struct ibv_mr     **post_array_mr;

    std::queue<pending_send> pending_queue;
    std::queue<int>          irecv_queue;
    //std::thread *poll_thread;
    std::thread *poll_send_thread;
    std::thread *poll_recv_thread;
    pool<addr_mr_pair>  addr_mr_pool;
    pool<ctl_flow_info> ctl_flow_pool;
    pool<mr_pair_recv>  recv_mr_pool;
    arraypool<irecv_info> irecv_info_pool;
    arraypool<isend_info> isend_info_pool;

    lock _lock;
    lock _lock_for_peer_num;

    int used_recv_num;
    size_t recvd_bufsize;
    int last_used_index;
    int peer_left_recv_num;
    std::queue<unsend_element> unsend_queue;

    //void poll_func(rdma_conn_p2p* conn);
    void poll_send_func(rdma_conn_p2p *conn);
    void poll_recv_func(rdma_conn_p2p *conn);

    void create_qp_info(unidirection_rdma_conn &rdma_conn_info, bool isrecvqp);
    void modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, int ib_port);
    void modify_qp_to_rts(struct ibv_qp *qp);
    void clean_used_fd();
    void reload_post_recv();

    int  pp_post_recv(struct ibv_qp *qp, uintptr_t buf_addr, uint32_t lkey, uint32_t len, struct ibv_mr *mr);
    int  pp_post_send(struct ibv_qp *qp, uintptr_t buf_addr, uint32_t lkey, uint32_t len, bool isinline, bool is_singal);

    int  pp_post_write(addr_mr_pair *mr_pair, uint64_t remote_addr, uint32_t rkey, uint32_t imm_data);
    bool do_send_completion(int n, struct ibv_wc *wc);
    bool do_recv_completion(int n, struct ibv_wc *wc);
    void pending_queue_not_empty(void *buf, size_t count, int index, non_block_handle *req);
    void irecv_queue_not_empty(enum RECV_TYPE type, struct ibv_wc *wc, int index);

public:
    rdma_conn_p2p(const rdma_conn_p2p&) = delete;
    rdma_conn_p2p(rdma_conn_p2p && ) = delete;
    rdma_conn_p2p & operator=(const rdma_conn_p2p&) = delete;

    rdma_conn_p2p();
    int isend(const void *buf, size_t count, non_block_handle *req);
    int irecv(void *buf, size_t count, non_block_handle *req);
    bool wait(non_block_handle* req);
    /*

    int oneside_send_pre(const void *buf, size_t count, non_block_handle *req, oneside_info *peer_info);
    int oneside_recv_pre(void *buf, size_t count, non_block_handle *req, oneside_info* my_info);
    bool end_oneside(oneside_info *peer_info);

    int oneside_isend(oneside_info *peer_info, non_block_handle *req);

    int hp_isend_init(non_block_handle* req, oneside_info *peer_info);
    int hp_irecv_init(void *buf, size_t count, non_block_handle *req,  int send_times, oneside_info *peer_info);
    int hp_isend(const void *buf, size_t count, non_block_handle *req, oneside_info* peer_info);
    bool hp_recv_wait(non_block_handle *req, int send_times);// recv side will wait send_times*/
};

#include "conn_system.h"
#endif //SENDRECV_RDMA_CONN_P2P_H
#endif
