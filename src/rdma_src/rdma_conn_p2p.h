#ifndef SENDRECV_RDMA_CONN_P2P_H
#define SENDRECV_RDMA_CONN_P2P_H

#include "rdma_conn_p2p.h"
#include "rdma_resource.h"
#include <queue>
#include <sendrecv.h>
#include <sys/eventfd.h>
#include "conn_system.h"

class rdma_conn_p2p {
    friend class conn_system;
private:
    int send_event_fd ;
    int recv_event_fd ;
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
    arraypool<irecv_info> irecv_info_pool;
    arraypool<isend_info> isend_info_pool;

    lock _lock;
    lock _lock_for_peer_num;
    //bool isruning;
    bool issend_running;
    bool isrecv_running;

    int used_recv_num;
    size_t recvd_bufsize;
    int last_used_index;
    int peer_left_recv_num;
    std::queue<unsend_element> unsend_queue;

    //void poll_func(rdma_conn_p2p* conn);
    void poll_send_func(rdma_conn_p2p *conn);
    void poll_recv_func(rdma_conn_p2p *conn);

    void nofity_system(int event_fd);
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

    /*for test real transfer_time*/
    double total_write_consume = 0.0;
    timer _tmp_start;

    double small_write_consume = 0.0;
    timer _small_start;

public:
    rdma_conn_p2p(const rdma_conn_p2p&) = delete;
    rdma_conn_p2p(rdma_conn_p2p && ) = delete;
    rdma_conn_p2p & operator=(const rdma_conn_p2p&) = delete;

    rdma_conn_p2p();
    int isend(const void *buf, size_t count, non_block_handle *req);
    int irecv(void *buf, size_t count, non_block_handle *req);
    bool wait(non_block_handle* req);

    int oneside_send_pre(const void *buf, size_t count, non_block_handle *req, oneside_info *peer_info);
    int oneside_recv_pre(void *buf, size_t count, non_block_handle *req, oneside_info* my_info);
    bool end_oneside(oneside_info *peer_info);

    int oneside_isend(oneside_info *peer_info, non_block_handle *req);
    bool wait_oneside_recv(oneside_info *peer_info);

    double get_write_time(){
        return total_write_consume;
    }
    double get_small_time(){
        return small_write_consume;
    }
};


#endif //SENDRECV_RDMA_CONN_P2P_H
