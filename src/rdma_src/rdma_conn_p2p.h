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
    unidirection_rdma_conn recv_rdma_conn;
    exchange_qp_data recv_direction_qp;

    void nofity_system(int event_fd);
    void create_qp_info(unidirection_rdma_conn &rdma_conn_info);
    void clean_used_fd();

public:
    rdma_conn_p2p(const rdma_conn_p2p&) = delete;
    rdma_conn_p2p(rdma_conn_p2p && ) = delete;
    rdma_conn_p2p & operator=(const rdma_conn_p2p&) = delete;

    rdma_conn_p2p();

    //bool isend();
    //bool irecv();
};


#endif //SENDRECV_RDMA_CONN_P2P_H
