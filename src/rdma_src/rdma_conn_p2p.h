#ifndef SENDRECV_RDMA_CONN_P2P_H
#define SENDRECV_RDMA_CONN_P2P_H

#include "rdma_conn_p2p.h"
#include "rdma_resource.h"
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
    ctl_flow_info    ctl_flow;
    struct ibv_mr    *ctl_flow_mr;

    unidirection_rdma_conn recv_rdma_conn;
    exchange_qp_data recv_direction_qp;
    status_recv_buf  recv_local_buf_status;
    send_req_clt_info *post_array; //used for recv_qp recvd req_msg and small info
    struct ibv_mr     *post_array_mr;

    void nofity_system(int event_fd);
    void create_qp_info(unidirection_rdma_conn &rdma_conn_info, bool isrecvqp);
    void modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, int ib_port);
    void modify_qp_to_rts(struct ibv_qp *qp);
    void clean_used_fd();

    int  pp_post_recv(struct ibv_qp *qp, uintptr_t buf_addr, uint32_t lkey, uint32_t len);
    int  pp_post_send();

public:
    rdma_conn_p2p(const rdma_conn_p2p&) = delete;
    rdma_conn_p2p(rdma_conn_p2p && ) = delete;
    rdma_conn_p2p & operator=(const rdma_conn_p2p&) = delete;

    rdma_conn_p2p();

    //bool isend();
    //bool irecv();
};


#endif //SENDRECV_RDMA_CONN_P2P_H
