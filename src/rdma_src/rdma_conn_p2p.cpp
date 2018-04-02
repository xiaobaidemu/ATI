#include "rdma_conn_p2p.h"
#define RX_DEPTH 500
#define MAX_INLINE_LEN 256
#define MAX_SGE_LEN    10
#define MAX_SMALLMSG_SIZE      (1024)
#define MAX_POST_RECV_NUM      (1024)
#define RECVD_BUF_SIZE    (1024*1024*256)
rdma_conn_p2p::rdma_conn_p2p() {
    send_event_fd = CCALL(eventfd(0, EFD_CLOEXEC));
    recv_event_fd = CCALL(eventfd(0, EFD_CLOEXEC));
}

void rdma_conn_p2p::nofity_system(int event_fd)
{
    int64_t value = 1;
    CCALL(write(event_fd, &value, sizeof(value)));
}

void rdma_conn_p2p::create_qp_info(unidirection_rdma_conn &rdma_conn_info, bool isrecvqp){
    //test whether there were ibv_device
    int num_devices;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        ERROR("Failed to get IB devices list\n");
        ASSERT(0);
    }

    rdma_conn_info.rx_depth = RX_DEPTH;
    struct ibv_device *ibv_dev;
    for (int i = 0; i < num_devices; i++) {
        ibv_dev = dev_list[i];
        rdma_conn_info.context = ibv_open_device(ibv_dev);
        if(rdma_conn_info.context)
            break;
    }
    if(!(rdma_conn_info.context)) {
        ERROR("Cannot open any ibv_device.\n");
        ASSERT(0);
    }
    rdma_conn_info.ib_port = 1;//default is 1

    CCALL(ibv_query_port(rdma_conn_info.context, rdma_conn_info.ib_port, &(rdma_conn_info.portinfo)));
    rdma_conn_info.channel = ibv_create_comp_channel(rdma_conn_info.context);
    ASSERT(rdma_conn_info.channel);

    rdma_conn_info.pd = ibv_alloc_pd(rdma_conn_info.context);
    ASSERT(rdma_conn_info.pd);

    rdma_conn_info.cq = ibv_create_cq(rdma_conn_info.context, rdma_conn_info.rx_depth+1, this,
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
    memset(&attr, 0, sizeof(attr));
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

    if(isrecvqp){ //means receiver need to malloc a piece of recvd_buffer space and ibv_post_recv
        void *cache_addr = malloc(RECVD_BUF_SIZE + MAX_SMALLMSG_SIZE);
        ibv_mr *buff_mr     = ibv_reg_mr(rdma_conn_info.pd, cache_addr, RECVD_BUF_SIZE + MAX_SMALLMSG_SIZE,
                                         IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);

        recv_local_buf_status.buf_info.addr = (uint64_t)cache_addr;
        recv_local_buf_status.buf_info.buff_mr = (uintptr_t)buff_mr;
        recv_local_buf_status.buf_info.size  = RECVD_BUF_SIZE;
        recv_local_buf_status.buf_info.rkey  = buff_mr->rkey;
        ASSERT(recv_local_buf_status.pos_irecv == 0);
        ASSERT(recv_local_buf_status.pos_isend == 0);

        //ibv_post_recv
        post_array = new send_req_clt_info[MAX_POST_RECV_NUM];
        post_array_mr  = ibv_reg_mr(rdma_conn_info.pd, post_array,
                                                   sizeof(send_req_clt_info) * MAX_POST_RECV_NUM,
                                                   IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
        for(int i = 0;i < MAX_POST_RECV_NUM;i++){
            CCALL(pp_post_recv(rdma_conn_info.qp, (uintptr_t)(post_array+i),post_array_mr->lkey,
                          sizeof(send_req_clt_info)));
        }
        modify_qp_to_rtr(rdma_conn_info.qp, recv_direction_qp.qpn, recv_direction_qp.lid, rdma_conn_info.ib_port);
        modify_qp_to_rts(rdma_conn_info.qp);
        ITRACE("Finish create the recv qp ~~~~~~~.\n");
    }
    else{
        //ibv_post_recv
        ctl_flow_mr = ibv_reg_mr(rdma_conn_info.pd, &ctl_flow, sizeof(ctl_flow),
                                 IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
        pp_post_recv(rdma_conn_info.qp, (uintptr_t)&ctl_flow, ctl_flow_mr->lkey, sizeof(ctl_flow));
        ITRACE("Finish Half create the send qp ~~~~~~~.\n");

    }


}

void rdma_conn_p2p::modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, int ib_port){
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_1024;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn   = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 0x12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = ib_port;
    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER;
    CCALL(ibv_modify_qp(qp, &attr, flags));
}

void rdma_conn_p2p::modify_qp_to_rts(struct ibv_qp *qp) {
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
            IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    CCALL(ibv_modify_qp(qp, &attr, flags));
}

int rdma_conn_p2p::pp_post_recv(struct ibv_qp *qp, uintptr_t buf_addr, uint32_t lkey, uint32_t len) {
    struct ibv_recv_wr wr, *bad_wr;
    memset(&wr, 0, sizeof(wr));
    struct ibv_sge list;
    list.addr = buf_addr;
    list.length = len;
    list.lkey = lkey;

    wr.wr_id   = (uintptr_t)this;
    wr.sg_list = &list;
    wr.num_sge = 1;

    int ret = 0;
    ret = ibv_post_recv(qp, &wr, &bad_wr);

    return ret;
}

int rdma_conn_p2p::pp_post_send() {

}

void rdma_conn_p2p::clean_used_fd(){
    send_socket_conn->async_close();
    recv_socket_conn->async_close();
}
