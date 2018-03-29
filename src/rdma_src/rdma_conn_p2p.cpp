#include "rdma_conn_p2p.h"
#define RX_DEPTH 500
#define MAX_INLINE_LEN 256
#define MAX_SGE_LEN    10
rdma_conn_p2p::rdma_conn_p2p() {
    send_event_fd = CCALL(eventfd(0, EFD_CLOEXEC));
    recv_event_fd = CCALL(eventfd(0, EFD_CLOEXEC));
}

void rdma_conn_p2p::nofity_system(int event_fd)
{
    int64_t value = 1;
    CCALL(write(event_fd, &value, sizeof(value)));
}

void rdma_conn_p2p::create_qp_info(unidirection_rdma_conn &rdma_conn_info){
    //test whether there were ibv_device
    rdma_conn_info.rx_depth = RX_DEPTH;
    rdma_conn_info.ib_port = 1;//default is 1

    CCALL(ibv_query_port(belong_to->get_ibv_ctx(), rdma_conn_info.ib_port, &(rdma_conn_info.portinfo)));
    rdma_conn_info.channel = ibv_create_comp_channel(belong_to->get_ibv_ctx());
    ASSERT(rdma_conn_info.channel);

    rdma_conn_info.pd = ibv_alloc_pd(belong_to->get_ibv_ctx());
    ASSERT(rdma_conn_info.pd);

    rdma_conn_info.cq = ibv_create_cq(belong_to->get_ibv_ctx(), rdma_conn_info.rx_depth+1, this,
                                      rdma_conn_info.channel, 0);
    ASSERT(rdma_conn_info.cq);

    struct ibv_qp_init_attr init_attr;
    memset(&init_attr, 0, sizeof(init_attr));
    init_attr.send_cq = rdma_conn_info.cq;
    init_attr.recv_cq = rdma_conn_info.cq;
    init_attr.cap.max_send_wr  = rdma_conn_info.rx_depth;
    init_attr.cap.max_recv_wr  = rdma_conn_info.rx_depth;
    init_attr.cap.max_send_sge = MAX_SGE_LEN;
    init_attr.cap.max_recv_wr  = MAX_SGE_LEN;
    init_attr.cap.max_inline_data = MAX_INLINE_LEN;
    init_attr.qp_type = IBV_QPT_RC;
    init_attr.sq_sig_all = 1;
    init_attr.qp_context = (void*)this;

    rdma_conn_info.qp = ibv_create_qp(rdma_conn_info.pd, &init_attr);
    ASSERT(rdma_conn_info.qp);


    struct ibv_qp_attr attr;
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num   = rdma_conn_info.ib_port;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

    if (ibv_modify_qp(rdma_conn_info.qp, &attr,
                      IBV_QP_STATE              |
                      IBV_QP_PKEY_INDEX         |
                      IBV_QP_PORT               |
                      IBV_QP_ACCESS_FLAGS)) {
        ERROR("Failed to modify QP to INIT\n");
        ASSERT(0);
    }
    ITRACE("Finish create the qp ~~~~~~~.\n");
}