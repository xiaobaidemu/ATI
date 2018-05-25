#ifdef IBEXIST
#include "rdma_conn_p2p.h"
#include "rdma_resource.h"
#include "errno.h"
#include <sys/sysinfo.h>

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
rdma_conn_p2p::rdma_conn_p2p():irecv_info_pool(2000), isend_info_pool(2000) {
    peer_left_recv_num = MAX_POST_RECV_NUM;
    used_recv_num = 0;
    recvd_bufsize = 0;
    last_used_index = 0;
    _lock_send_ech.acquire();
    _lock_recv_ech.acquire();
}

void rdma_conn_p2p::create_qp_info(unidirection_rdma_conn &rdma_conn_info, bool isrecvqp){
    //test whether there were ibv_device
    struct ibv_qp_init_attr init_attr;
    memset(&init_attr, 0, sizeof(init_attr));
    if(isrecvqp){
        init_attr.send_cq = conn_sys->cq_recv_qp;
        init_attr.recv_cq = conn_sys->cq_recv_qp;
    }
    else{
        init_attr.send_cq = conn_sys->cq_send_qp;
        init_attr.recv_cq = conn_sys->cq_send_qp;
    }
    init_attr.cap.max_send_wr  = MAX_POST_RECV_NUM*2 ;
    init_attr.cap.max_recv_wr  = MAX_POST_RECV_NUM*3 ;
    init_attr.cap.max_send_sge = MAX_SGE_LEN;
    init_attr.cap.max_inline_data = MAX_INLINE_LEN;
    init_attr.qp_type = IBV_QPT_RC;
    init_attr.sq_sig_all = 1;
    init_attr.qp_context = (void*)this;

    rdma_conn_info.qp = ibv_create_qp(conn_sys->pd, &init_attr);
    //ERROR("errno: %s\n",strerror(errno));
    ASSERT(rdma_conn_info.qp);

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num   = conn_sys->ib_port;
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
        char *cache_addr = (char*)malloc(RECVD_BUF_SIZE + MAX_SMALLMSG_SIZE);
        ibv_mr *buff_mr  = ibv_reg_mr(conn_sys->pd, cache_addr, RECVD_BUF_SIZE + MAX_SMALLMSG_SIZE,
                                         IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);

        recv_local_buf_status.buf_info.addr = (uint64_t)cache_addr;
        recv_local_buf_status.buf_info.buff_mr = (uintptr_t)buff_mr;
        recv_local_buf_status.buf_info.size  = RECVD_BUF_SIZE;
        recv_local_buf_status.buf_info.rkey  = buff_mr->rkey;
        ASSERT(recv_local_buf_status.pos_irecv == 0);
        ASSERT(recv_local_buf_status.pos_isend == 0);

        //ibv_post_recv
        post_array = new send_req_clt_info[MAX_POST_RECV_NUM];

        post_array_mr = new struct ibv_mr*[MAX_POST_RECV_NUM];

        for(int i = 0;i < MAX_POST_RECV_NUM;i++){
            post_array_mr[i] =  ibv_reg_mr(conn_sys->pd, &(post_array[i]), sizeof(send_req_clt_info), IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
            CCALL(pp_post_recv(rdma_conn_info.qp, (uintptr_t)(&post_array[i]), post_array_mr[i]->lkey,
                          sizeof(send_req_clt_info), post_array_mr[i]));
        }
        modify_qp_to_rtr(rdma_conn_info.qp, recv_direction_qp.qpn, recv_direction_qp.lid, conn_sys->ib_port);
        modify_qp_to_rts(rdma_conn_info.qp);
        SUCC("Finish create the recv qp ~~~~~~~.\n");
    }
    else{
        //ibv_post_recv
        ctl_flow_info *ctl_flow = ctl_flow_pool.pop();ASSERT(ctl_flow);
        struct ibv_mr *ctl_flow_mr = ibv_reg_mr(conn_sys->pd, ctl_flow, sizeof(ctl_flow_info),
                                 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
        CCALL(pp_post_recv(rdma_conn_info.qp, (uintptr_t)ctl_flow, ctl_flow_mr->lkey, sizeof(ctl_flow_info), ctl_flow_mr));
        SUCC("Finish Half create the send qp ~~~~~~~.\n");
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
    attr.min_rnr_timer = 12;
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
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14;
    attr.retry_cnt = 1;
    attr.rnr_retry = 0;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
            IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    CCALL(ibv_modify_qp(qp, &attr, flags));
}

int rdma_conn_p2p::pp_post_recv(struct ibv_qp *qp, uintptr_t buf_addr, uint32_t lkey, uint32_t len, struct ibv_mr* mr) {
    struct ibv_recv_wr wr, *bad_wr = nullptr;
    memset(&wr, 0, sizeof(wr));
    struct ibv_sge list;
    list.addr = buf_addr;
    list.length = len;
    list.lkey = lkey;

    wr.wr_id   = (uintptr_t)mr;
    wr.sg_list = &list;
    wr.num_sge = 1;

    int ret = 0;
    ret = ibv_post_recv(qp, &wr, &bad_wr);
    if(bad_wr){
        ERROR("CAN NOT POST_RECV.\n");
        return -1;
    }
    return ret;
}

int rdma_conn_p2p::pp_post_send(struct ibv_qp *qp, uintptr_t buf_addr, uint32_t lkey, uint32_t len, bool isinline, bool is_singal) {
    struct ibv_send_wr wr, *bad_wr = nullptr;
    memset(&wr, 0, sizeof(wr));
    struct ibv_sge list;
    if(isinline){
        wr.send_flags = IBV_SEND_INLINE;
        wr.wr_id = (uintptr_t)nullptr;
    }
    else
        wr.wr_id = buf_addr;
    //if(is_singal) wr.send_flags |= IBV_SEND_SIGNALED;
    wr.sg_list = &list;
    wr.num_sge = 1;
    wr.opcode  = IBV_WR_SEND;

    list.addr = buf_addr;
    list.length = len;
    list.lkey = lkey;//no important

    int rc = ibv_post_send(qp, &wr, &bad_wr);
    if(bad_wr){
        ERROR("CAN NOT IBV_POST_SEND.\n");
        return -1;
    }
    return rc;
}

int rdma_conn_p2p::pp_post_write(addr_mr_pair *mr_pair, uint64_t remote_addr, uint32_t rkey, uint32_t imm_data){
    struct ibv_send_wr wr, *bad_wr = nullptr;
    struct ibv_sge list;
    //memset(&list, 0, sizeof(list));
    memset(&wr, 0, sizeof(wr));
    list.addr   = mr_pair->send_addr;
    list.length = mr_pair->len;
    list.lkey   = mr_pair->send_mr->lkey;

    //wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr_id = (uintptr_t)mr_pair;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.imm_data = htonl(imm_data);
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = rkey;
    wr.sg_list = &list;
    wr.num_sge = 1;
    return ibv_post_send(send_rdma_conn.qp, &wr, &bad_wr);
}
void rdma_conn_p2p::clean_used_fd(){
    send_socket_conn->async_close();
    recv_socket_conn->async_close();
}

/*

bool rdma_conn_p2p::do_send_completion(int n, struct ibv_wc *wc_send){
    for(int i = 0;i < n;i++){
        struct ibv_wc *wc = wc_send + i;
        if(wc->status != IBV_WC_SUCCESS){
            ERROR("some error when do_send_completion (%s).\n",ibv_wc_status_str(wc->status));
            return false;
        }
        enum ibv_wc_opcode op = wc->opcode;
        if(op == IBV_WC_SEND)
            continue;
        if(op == IBV_WC_RECV) {
            struct ibv_mr *recv_mr = (struct ibv_mr *) (wc->wr_id);
            ctl_flow_info *ack_ctl_info = (ctl_flow_info *) recv_mr->addr;
            //ctl_msg related to used_recv_num and recvd_bufsize
            //WARN("xxxxxx %d\n",ack_ctl_info->type);
            if (ack_ctl_info->type == 0) {
                int tmp_used_recv = ack_ctl_info->ctl.used_recv_num;
                size_t tmp_recvd_bufsize = ack_ctl_info->ctl.recvd_bufsize;
                //change the send_peer_buf_status.pos_irecv

                send_peer_buf_status.pos_irecv = (send_peer_buf_status.pos_irecv + tmp_recvd_bufsize)%send_peer_buf_status.buf_info.size;

                //recycle the recv_mr
                memset(ack_ctl_info, 0, sizeof(ctl_flow_info));
                CCALL(pp_post_recv(send_rdma_conn.qp, (uintptr_t)ack_ctl_info, recv_mr->lkey,
                             sizeof(ctl_flow_info), recv_mr));

                _lock_for_peer_num.acquire();
                peer_left_recv_num += tmp_used_recv;
                WARN("peer_left_recv_num: %d.\n", peer_left_recv_num);
                while(!unsend_queue.empty()){
                    if(peer_left_recv_num > 0){
                        peer_left_recv_num--;
                        unsend_element element = unsend_queue.front();
                        unsend_queue.pop();
                        if(element.is_real_msg){
                            CCALL(pp_post_write(element.real_msg_info.mr_pair, element.real_msg_info.remote_addr,
                                          element.real_msg_info.rkey, element.real_msg_info.imm_data));
                            ITR_POLL("(sending real big/small msg from unsend_queue): recv_buffer %llx, rkey %x, len %d\n",
                                     (long long)element.real_msg_info.remote_addr, element.real_msg_info.rkey, element.real_msg_info.mr_pair->len);
                        }
                        else{
                            CCALL(pp_post_send(send_rdma_conn.qp, (uintptr_t)&(element.req_msg_info.req_msg), 0, sizeof(send_req_clt_info), true, false));
                            ITR_SEND("(BIG_MSG_REQ sending from unsend_queue): send_addr %llx, len %d, isend_index %d\n",(long long)element.req_msg_info.req_msg.send_addr,
                                     (int)element.req_msg_info.req_msg.len, element.req_msg_info.req_msg.isend_index);
                        }
                        //WARN("xxx peer_left_recv_num :%d size of unsend_queue:%d.\n", peer_left_recv_num, unsend_queue.size());
                    }
                    else break;
                }
                _lock_for_peer_num.release();

            } else {
                ASSERT(ack_ctl_info->type);
                if(ack_ctl_info->big.is_oneside){
                    ITR_POLL("this ack_ctl_info is a one_side pre msg from recv_peer.\n");
                    //restore the info from peer
                    uint32_t send_index = ack_ctl_info->big.send_index;
                    isend_info *isend_ptr = isend_info_pool.get(send_index);
                    oneside_info* peer_info = (oneside_info*)isend_ptr->req_handle->oneside_info_addr;
                    ASSERT(peer_info);

                    peer_info->set_oneside_info(ack_ctl_info->big.recv_buffer, ack_ctl_info->big.rkey,
                                                ack_ctl_info->big.send_buffer, ack_ctl_info->big.send_mr,
                                                ack_ctl_info->big.index, ack_ctl_info->big.send_index, ONE_SIDE_SEND);
                    isend_ptr->req_handle->_lock.release();
                    memset(ack_ctl_info, 0, sizeof(ctl_flow_info));
                    ctl_flow_pool.push(ack_ctl_info);
                    continue;
                    
                }
                addr_mr_pair *mr_pair = addr_mr_pool.pop();//remember to recycle
                ASSERT(mr_pair);
                mr_pair->send_addr = ack_ctl_info->big.send_buffer;
                mr_pair->send_mr = ack_ctl_info->big.send_mr;
                mr_pair->len = ack_ctl_info->big.send_mr->length;
                mr_pair->isend_index = ack_ctl_info->big.send_index;
                uint32_t imm_data = ack_ctl_info->big.index;
                imm_data |= IMM_DATA_MAX_MASK;
                //ERROR("imm_data: %d %d\n", imm_data, ack_ctl_info->big.index);

                _lock_for_peer_num.acquire();
                if(peer_left_recv_num <= 0){
                    //WARN("===============\n");
                    unsend_queue.emplace(mr_pair, ack_ctl_info->big.recv_buffer,
                                         ack_ctl_info->big.rkey, imm_data);
                    _lock_for_peer_num.release();
                }
                else{
                    peer_left_recv_num--;
                    _lock_for_peer_num.release();
                    CCALL(pp_post_write(mr_pair, ack_ctl_info->big.recv_buffer, ack_ctl_info->big.rkey, imm_data));
                    ITR_POLL("sending real big msg: recv_buffer %llx, rkey %x, len %d\n",
                             (long long)ack_ctl_info->big.recv_buffer, ack_ctl_info->big.rkey, mr_pair->len);
                }
                //push the ack_ctl_info
                memset(ack_ctl_info, 0, sizeof(ctl_flow_info));
                ctl_flow_pool.push(ack_ctl_info);
            }
        }
        else if(op == IBV_WC_RDMA_WRITE){
            //means small msg or big msg have sent
            addr_mr_pair *mr_pair = (addr_mr_pair*)(wc->wr_id);
            int isend_index = mr_pair->isend_index;
            //WARN("========= isend_index %d\n", isend_index);
            memset(mr_pair, 0, sizeof(addr_mr_pair));
            addr_mr_pool.push(mr_pair);
            isend_info_pool.get(isend_index)->req_handle->_lock.release();
        }else{
            ERROR("unknown type when do_recv_completion");
            return false;
        }
    }
    return true;
}

void rdma_conn_p2p::irecv_queue_not_empty(enum RECV_TYPE type, struct ibv_wc *wc, int index){
    int recv_index = irecv_queue.front();
    irecv_queue.pop();
    irecv_info *irecv_ptr = irecv_info_pool.get(recv_index);
    //SUCC("irecv_queue_not_empty irecv_ptr_addr %llx.\n", (long long)irecv_ptr);
    ASSERT(irecv_ptr);
    if(type == SEND_REQ_MSG){
        struct ibv_mr* recv_mr = (struct ibv_mr*)(wc->wr_id);
        send_req_clt_info *send_req = (send_req_clt_info*)(recv_mr->addr);
        ASSERT(recv_mr);

        ctl_flow_info ack_ctl_info;
        ack_ctl_info.type = 1;
        if(!irecv_ptr->recv_mr){
            irecv_ptr->recv_mr = ibv_reg_mr(recv_rdma_conn.pd, (void*)irecv_ptr->recv_addr, irecv_ptr->recv_size, IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
        }
        //ITR_SPECIAL("[irecv_queue_not_empty] irecv_ptr belong to index %d, %llx\n", recv_index, (long long)irecv_ptr);
        ASSERT(irecv_ptr->recv_mr);
        ack_ctl_info.big.rkey = irecv_ptr->recv_mr->rkey;
        ack_ctl_info.big.recv_buffer = irecv_ptr->recv_addr;
        ack_ctl_info.big.send_buffer = send_req->send_addr;
        ack_ctl_info.big.send_mr   = send_req->send_mr;
        ack_ctl_info.big.index     = recv_index;//means recv index
        ack_ctl_info.big.send_index = send_req->isend_index;
        ack_ctl_info.big.is_oneside = send_req->is_oneside;

        CCALL(pp_post_send(recv_rdma_conn.qp, (uintptr_t)&ack_ctl_info, 0, sizeof(ack_ctl_info), true, false));

        if(send_req->is_oneside){
            ASSERT(irecv_ptr->req_handle->type == WAIT_ONESIDE_RECV_PRE || irecv_ptr->req_handle->type == WAIT_HP_IRECV_INIT);
            oneside_info* my_info = (oneside_info*)irecv_ptr->req_handle->oneside_info_addr;
            ASSERT(my_info);
            my_info->set_oneside_info(ack_ctl_info.big.recv_buffer, ack_ctl_info.big.rkey,
                                      ack_ctl_info.big.send_buffer, ack_ctl_info.big.send_mr,
                                      ack_ctl_info.big.index, ack_ctl_info.big.send_index, ONE_SIDE_RECV);
            irecv_ptr->req_handle->_lock.release();
            ITR_POLL("([ONESIDE pre] sending in thread...) rkey %x, recv_addr %llx send_addr %llx recv_index %d.\n",
                     (int)ack_ctl_info.big.rkey, (long long)ack_ctl_info.big.recv_buffer, (long long)ack_ctl_info.big.send_buffer, ack_ctl_info.big.index);
        }
        else{
            ITR_POLL("(BIG_MSG_ACK sending in thread...) rkey %x, recv_addr %llx send_addr %llx recv_index %d.\n",
                     (int)ack_ctl_info.big.rkey, (long long)ack_ctl_info.big.recv_buffer, (long long)ack_ctl_info.big.send_buffer, ack_ctl_info.big.index);
        }

    } else{
        ITR_POLL("irecv_queue not empty Receiving ...... \n");
        ASSERT(type == SMALL_WRITE_IMM);
        ASSERT(recv_local_buf_status.pos_isend == recv_local_buf_status.pos_irecv);
        memcpy((void*)irecv_ptr->recv_addr,
               (char*)recv_local_buf_status.buf_info.addr + recv_local_buf_status.pos_isend, index);
        irecv_ptr->req_handle->_lock.release();
        recvd_bufsize += index;
        recv_local_buf_status.pos_isend = (recv_local_buf_status.pos_isend + index) % recv_local_buf_status.buf_info.size;
        recv_local_buf_status.pos_irecv = (recv_local_buf_status.pos_irecv + index) % recv_local_buf_status.buf_info.size;
    }
    return;
}

bool rdma_conn_p2p::do_recv_completion(int n, struct ibv_wc *wc_recv){
    for(int i = 0;i < n;i++){
        struct ibv_wc *wc = wc_recv+i;
        if(wc->status != IBV_WC_SUCCESS){
            ERROR("some error when do_recv_completion (%s).\n",ibv_wc_status_str(wc->status));
            return false;
        }
        enum ibv_wc_opcode op = wc->opcode;
        if(op == IBV_WC_SEND)
            continue;
        enum RECV_TYPE type;
        int index = -1;
        if(op == IBV_WC_RECV_RDMA_WITH_IMM){
            uint32_t imm_data = ntohl(wc->imm_data);
            int type_bit = imm_data >> 31;
            if(type_bit){//RECV BIG_MSG
                index = imm_data & IMM_DATA_SMALL_MASK;
                type  = BIG_WRITE_IMM;
            }
            else{
                index = imm_data; //small_msg_size
                type = SMALL_WRITE_IMM;
            }
        }
        else if(op == IBV_WC_RECV){
            type = SEND_REQ_MSG;
        }
        else{
            ERROR("capture some unknown op when do_recv_completion.\n");
            return false;
        }
        //the _lock need to be reconsidered
        _lock.acquire();
        used_recv_num++;
        _lock.release();
        if(type == BIG_WRITE_IMM){
            irecv_info *big_msg_ptr = irecv_info_pool.get(index);
            ASSERT(big_msg_ptr);
            //ITR_SPECIAL("big_msg_ptr index:%d, big_msg_ptr_addr:%llx, recv_addr %llx\n", big_msg_ptr->req_handle->index, (long long)big_msg_ptr, (long long)big_msg_ptr->recv_addr);
            ASSERT(big_msg_ptr->recv_mr);
            big_msg_ptr->req_handle->hp_cur_times++;
            big_msg_ptr->req_handle->_lock.release();
        }
        else{
            if(irecv_queue.empty()){
                //double check
                _lock.acquire();
                if(irecv_queue.empty()){
                    if(type == SEND_REQ_MSG){
                        //push the big_msg_req into pending_queue
                        struct ibv_mr* recv_mr = (struct ibv_mr*)(wc->wr_id);
                        ASSERT(recv_mr);
                        send_req_clt_info *recvd_req = (send_req_clt_info*)recv_mr->addr;
                        pending_send pending_req;
                        pending_req.is_big = true;
                        ASSERT(recvd_req->send_addr);ASSERT(recvd_req->send_mr);
                        pending_req.big.big_addr = recvd_req->send_addr;
                        pending_req.big.big_mr   = recvd_req->send_mr;
                        pending_req.big.size     = recvd_req->len;
                        pending_req.big.isend_index = recvd_req->isend_index;
                        pending_req.big.is_oneside  = recvd_req->is_oneside;
                        pending_queue.push(pending_req);
                    } else{
                        //push the small_msg into pending_queue
                        ITR_POLL("irecv_queue is empty, Receiving.............\n");
                        pending_send pending_req;
                        pending_req.is_big = false;
                        pending_req.small.size = index;//index means the size of the small msg
                        pending_req.small.pos  = (char*)recv_local_buf_status.buf_info.addr + recv_local_buf_status.pos_isend;
                        recv_local_buf_status.pos_isend = (recv_local_buf_status.pos_isend + index) % recv_local_buf_status.buf_info.size;
                        pending_queue.push(pending_req);
                    }
                }
                else{
                    _lock.release();
                    irecv_queue_not_empty(type, wc, index);
                    if(recvd_bufsize >= THREHOLD_RECVD_BUFSIZE || used_recv_num >= MAX_POST_RECV_NUM){
                        ITR_SPECIAL("### (in poll)used_recv_num (%d), recvd_bufsize (%lld), feedback the situation to sender.###\n",
                                    used_recv_num, (long long)recvd_bufsize);
                        reload_post_recv();
                    }
                    return 1;
                }
                _lock.release();
            }
            else{//irecv_queue is not empty
                irecv_queue_not_empty(type, wc, index);
            }
        }
        if(recvd_bufsize >= THREHOLD_RECVD_BUFSIZE || used_recv_num >= MAX_POST_RECV_NUM){
            ITR_SPECIAL("### (in poll)used_recv_num (%d), recvd_bufsize (%lld), feedback the situation to sender.###\n",
                        used_recv_num, (long long)recvd_bufsize);
            reload_post_recv();
        }
        //_lock.release();
    }
    return true;
}

int rdma_conn_p2p::isend(const void *buf, size_t count, non_block_handle *req){
    req->_lock.release();
    req->_lock.acquire();

    int isend_index = isend_info_pool.pop();
    isend_info *isend_ptr = isend_info_pool.get(isend_index); ASSERT(isend_ptr);
    isend_ptr->send_addr  = (uintptr_t)const_cast<void*>(buf);
    isend_ptr->send_size  = count;
    isend_ptr->send_mr    = nullptr;
    isend_ptr->req_handle = req;
    req->index            = isend_index;
    req->type             = WAIT_ISEND;
    req->oneside_info_addr = 0;

    //judge the buf is small or big

    bool isbig;
    size_t n = send_peer_buf_status.buf_info.size;
    size_t pos_r = send_peer_buf_status.pos_irecv;
    size_t pos_s = send_peer_buf_status.pos_isend;
    if(count >= MAX_SMALLMSG_SIZE){
        isbig = true;
    }
    else{
        size_t left_recvd_size = n - (pos_s + n - pos_r) % n;
        if(left_recvd_size > count)
            isbig = false;
        else
            isbig = true;
    }

    if(isbig){
        //before post_send ,need to post_recv on send_rdma_qp.qp, this part can be optimized
        //use pool to change the new op
        //ctl_flow_info *ack_ctl_addr = new ctl_flow_info(); ASSERT(ack_ctl_addr);
        ctl_flow_info *ack_ctl_addr = ctl_flow_pool.pop();ASSERT(ack_ctl_addr);
        struct ibv_mr *mr = ibv_reg_mr(send_rdma_conn.pd, ack_ctl_addr, sizeof(ctl_flow_info), IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
        CCALL(pp_post_recv(send_rdma_conn.qp, (uintptr_t)ack_ctl_addr, mr->lkey, sizeof(ctl_flow_info), mr));

        send_req_clt_info req_msg;
        req_msg.len = count;
        req_msg.send_addr  = (uintptr_t)const_cast<void*>(buf);
        req_msg.send_mr    = ibv_reg_mr(send_rdma_conn.pd, const_cast<void*>(buf), count, IBV_ACCESS_LOCAL_WRITE);
        req_msg.isend_index = isend_index;
        req_msg.is_oneside  = false;
        isend_ptr->send_mr  = req_msg.send_mr;

        _lock_for_peer_num.acquire();
        if(peer_left_recv_num <= 0){
            unsend_queue.emplace(req_msg);
            _lock_for_peer_num.release();
            return 1;
        }
        peer_left_recv_num--;
        //ITR_SEND("peer_lefe_recv_num %d\n", peer_left_recv_num);
        _lock_for_peer_num.release();

        CCALL(pp_post_send(send_rdma_conn.qp, (uintptr_t)&req_msg, 0, sizeof(req_msg), true, false));
        ITR_SEND("(BIG_MSG_REQ sending...) send_addr %llx, len %d, isend_index %d\n",(long long)req_msg.send_addr,(int)count, isend_index);
    }
    else{
        //this part should be reconsider
        addr_mr_pair *mr_pair = addr_mr_pool.pop();//remember to recycle
        ASSERT(mr_pair);
        mr_pair->send_addr = (uintptr_t)const_cast<void*>(buf);
        mr_pair->send_mr   = ibv_reg_mr(send_rdma_conn.pd, const_cast<void*>(buf), count, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
        isend_ptr->send_mr = mr_pair->send_mr;
        ASSERT((uintptr_t)mr_pair->send_mr->addr == (uintptr_t)mr_pair->send_addr);
        mr_pair->len       = count;
        mr_pair->isend_index = isend_index;
        uint32_t  imm_data = 0;//IMM_DATA_SMALL_MASK
        imm_data |= count;

        _lock_for_peer_num.acquire();
        if(peer_left_recv_num <= 0){
            unsend_queue.emplace(mr_pair, send_peer_buf_status.buf_info.addr + pos_s,
                                 send_peer_buf_status.buf_info.rkey, imm_data);
            _lock_for_peer_num.release();
            return 1;
        }
        peer_left_recv_num--;
        _lock_for_peer_num.release();

        CCALL(pp_post_write(mr_pair, send_peer_buf_status.buf_info.addr + pos_s,
                      send_peer_buf_status.buf_info.rkey, imm_data));
        send_peer_buf_status.pos_isend = (pos_s + count) % n;
        ITR_SEND("(SMALL_MSG sending...) send_addr %llx, len %d, isend_index %d\n", (long long unsigned int)mr_pair->send_addr, (int)count, isend_index);
    }
    return 1;
}


void rdma_conn_p2p::pending_queue_not_empty(void *buf, size_t count, int index, non_block_handle *req){
    pending_send one_pending = pending_queue.front();
    pending_queue.pop();
    if(one_pending.is_big){//this is big_msg_req
        //register the buf and ready to post_send buf
        struct ibv_mr *recv_mr;
        irecv_info *irecv_ptr = irecv_info_pool.get(index);
        if(!irecv_ptr->recv_mr){
            recv_mr = ibv_reg_mr(recv_rdma_conn.pd, buf, count,
                                 IBV_ACCESS_LOCAL_WRITE |IBV_ACCESS_REMOTE_WRITE);
            irecv_ptr->recv_mr = recv_mr;
        }
        //ITR_SPECIAL("irecv_ptr belong to index %d, %llx\n", index, (long long)irecv_ptr->recv_mr);
        ctl_flow_info ack_ctl_info;
        ack_ctl_info.type = 1;
        ack_ctl_info.big.rkey = irecv_ptr->recv_mr->rkey;
        ack_ctl_info.big.recv_buffer = (uintptr_t)buf;
        ack_ctl_info.big.send_buffer = one_pending.big.big_addr;
        ack_ctl_info.big.send_mr   = one_pending.big.big_mr;
        ack_ctl_info.big.index     = index;
        ack_ctl_info.big.send_index = one_pending.big.isend_index;
        ack_ctl_info.big.is_oneside = one_pending.big.is_oneside;

        CCALL(pp_post_send(recv_rdma_conn.qp, (uintptr_t)&ack_ctl_info, 0, sizeof(ack_ctl_info), true, false));

        if(one_pending.big.is_oneside){
            ASSERT(irecv_ptr->req_handle->type == WAIT_ONESIDE_RECV_PRE || irecv_ptr->req_handle->type == WAIT_HP_IRECV_INIT);
            oneside_info* my_info = (oneside_info*)irecv_ptr->req_handle->oneside_info_addr;
            ASSERT(my_info);
            my_info->set_oneside_info(ack_ctl_info.big.recv_buffer, ack_ctl_info.big.rkey,
                                      ack_ctl_info.big.send_buffer, ack_ctl_info.big.send_mr,
                                      ack_ctl_info.big.index, ack_ctl_info.big.send_index, ONE_SIDE_RECV);
            irecv_ptr->req_handle->_lock.release();
            ITR_RECV("ONESIDE MSG recving...) rkey %x, recv_addr %llx, send_addr %llx, recv_index %d.\n",
                     (int)ack_ctl_info.big.rkey, (long long)ack_ctl_info.big.recv_buffer, (long long)ack_ctl_info.big.send_buffer, ack_ctl_info.big.index);
        }
        else{
            ITR_RECV("(BIG_MSG_ACK recving...) rkey %x, recv_addr %llx, send_addr %llx, recv_index %d.\n",
                     (int)ack_ctl_info.big.rkey, (long long)ack_ctl_info.big.recv_buffer, (long long)ack_ctl_info.big.send_buffer, ack_ctl_info.big.index);
        }
    }
    else{ // this is small_msg
        //WARN("irecv: pending_queue not empty.\n");
        memcpy(buf, one_pending.small.pos, one_pending.small.size);
        req->_lock.release();
        // after memcpy, check whether need to post and return used_recv_num, recv_bufsize
        recvd_bufsize += one_pending.small.size;
        recv_local_buf_status.pos_irecv = (recv_local_buf_status.pos_irecv + one_pending.small.size)
                                          % recv_local_buf_status.buf_info.size;
        if(recvd_bufsize >= THREHOLD_RECVD_BUFSIZE){
            ITR_SPECIAL("### (in irecv)used_recv_num (%d), recvd_bufsize (%lld), feedback the situation to sender.###\n",
                        used_recv_num, (long long)recvd_bufsize);
            _lock.acquire();
            reload_post_recv();
            _lock.release();
        }
    }
    return;
}
int rdma_conn_p2p::irecv(void *buf, size_t count, non_block_handle *req){
    //_lock.acquire();
    req->_lock.release();
    req->_lock.acquire();
    
    int index = irecv_info_pool.pop();
    irecv_info *irecv_ptr = irecv_info_pool.get(index); 
    irecv_ptr->recv_addr = (uintptr_t)buf;
    irecv_ptr->recv_size = count;
    irecv_ptr->recv_mr   = nullptr;
    irecv_ptr->req_handle  = req;
    req->index = index;
    req->type  = WAIT_IRECV;

    if(pending_queue.empty()){
        _lock.acquire();
        if(pending_queue.empty()){
            //create a index for current irecv
            irecv_queue.push(index);
        }
        else {
            _lock.release();
            pending_queue_not_empty(buf, count, index, req);
            return 1;
        }
        _lock.release();
        return 1;
    }
    else{//pending_queue is not empty
        pending_queue_not_empty(buf, count, index, req);
    }
    //_lock.release();
    return 1;
}

void rdma_conn_p2p::reload_post_recv(){
    ctl_flow_info ctl_info;
    ctl_info.type = 0;
    ctl_info.ctl.used_recv_num = used_recv_num;
    ctl_info.ctl.recvd_bufsize = recvd_bufsize;
    int tmp_used_index = last_used_index + used_recv_num;
    if(tmp_used_index >= MAX_POST_RECV_NUM){
        tmp_used_index %= MAX_POST_RECV_NUM;
        for(int j = last_used_index; j < MAX_POST_RECV_NUM;j++)
            CCALL(pp_post_recv(recv_rdma_conn.qp, (uintptr_t)(&post_array[j]), post_array_mr[j]->lkey,
                         sizeof(send_req_clt_info), post_array_mr[j]));
        for(int j = 0; j < tmp_used_index;j++)
            CCALL(pp_post_recv(recv_rdma_conn.qp, (uintptr_t)(&post_array[j]), post_array_mr[j]->lkey,
                         sizeof(send_req_clt_info), post_array_mr[j]));
    }
    else{
        for(int j = last_used_index;j < tmp_used_index;j++)
            CCALL(pp_post_recv(recv_rdma_conn.qp, (uintptr_t)(&post_array[j]), post_array_mr[j]->lkey,
                         sizeof(send_req_clt_info), post_array_mr[j]));
    }
    last_used_index = tmp_used_index;
    CCALL(pp_post_send(recv_rdma_conn.qp, (uintptr_t)&ctl_info, 0, sizeof(ctl_info), true, false));
    WARN("last_used_index %d, used_recv_num %d.\n", last_used_index, used_recv_num);
    recvd_bufsize = 0;
    used_recv_num = 0;
}

bool rdma_conn_p2p::wait(non_block_handle* req){
    enum WAIT_TYPE type = req->type;
    if(type == WAIT_IRECV){
        //WARN("wait for irecv finished...\n");
        req->_lock.acquire();
        irecv_info *my_recv_info = irecv_info_pool.get(req->index);
        if(my_recv_info->recv_mr)
            CCALL(ibv_dereg_mr(my_recv_info->recv_mr));
        memset(my_recv_info, 0, sizeof(irecv_info));
        irecv_info_pool.push(req->index);
        //SUCC("(irecv) wait finished , index %d\n", req->index);
    }
    else if(type == WAIT_ISEND  || type == WAIT_HP_ISEND){
        //WARN("wait for isend finished...\n");
        req->_lock.acquire();
        isend_info *my_send_info = isend_info_pool.get(req->index);
        ASSERT(my_send_info->send_mr);
        CCALL(ibv_dereg_mr(my_send_info->send_mr));
        memset(my_send_info, 0, sizeof(irecv_info));
        isend_info_pool.push(req->index);
        //SUCC("(isend) wait finished , index %d\n", req->index);
    }
    else if(type == WAIT_HP_ISEND_INIT){
        req->_lock.acquire();
        isend_info *my_send_info = isend_info_pool.get(req->index);
        memset(my_send_info, 0, sizeof(irecv_info));
        isend_info_pool.push(req->index);
    }
    else {
        req->_lock.acquire();
        return true;
    }
    req->_lock.release();
    return true;
}




int rdma_conn_p2p::oneside_send_pre(const void *buf, size_t count, non_block_handle *req, oneside_info *peer_info){
    req->_lock.release();
    req->_lock.acquire();

    int isend_index = isend_info_pool.pop();
    isend_info *isend_ptr = isend_info_pool.get(isend_index);
    ASSERT(isend_ptr);
    isend_ptr->send_addr  = (uintptr_t)const_cast<void*>(buf);
    isend_ptr->send_size  = count;
    isend_ptr->send_mr    = nullptr;
    isend_ptr->req_handle = req;
    req->index            = isend_index;
    req->type             = WAIT_ONESIDE_SEND_PRE;
    req->oneside_info_addr = (uintptr_t)peer_info;

    //before post_send ,need to post_recv on send_rdma_qp.qp, this part can be optimized
    ctl_flow_info *ack_ctl_addr = ctl_flow_pool.pop(); ASSERT(ack_ctl_addr);
    struct ibv_mr *mr = ibv_reg_mr(send_rdma_conn.pd, ack_ctl_addr, sizeof(ctl_flow_info), IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
    pp_post_recv(send_rdma_conn.qp, (uintptr_t)ack_ctl_addr, mr->lkey, sizeof(ctl_flow_info), mr);

    send_req_clt_info req_msg;
    req_msg.len = count;
    req_msg.send_addr  = (uintptr_t)const_cast<void*>(buf);
    req_msg.send_mr    = ibv_reg_mr(send_rdma_conn.pd, const_cast<void*>(buf), count, IBV_ACCESS_LOCAL_WRITE);
    isend_ptr->send_mr = req_msg.send_mr;
    req_msg.isend_index = isend_index;
    req_msg.is_oneside  = true;

    _lock_for_peer_num.acquire();
    if(peer_left_recv_num <= 0){
        unsend_queue.emplace(req_msg);
        _lock_for_peer_num.release();
        return 1;
    }
    peer_left_recv_num--;
    _lock_for_peer_num.release();

    pp_post_send(send_rdma_conn.qp, (uintptr_t)&req_msg, 0, sizeof(req_msg), true, false);
    ITR_SEND("(iwrite_pre sending...) send_addr %llx, len %d, isend_index %d\n",(long long)req_msg.send_addr,(int)count, isend_index);
    return 1;
}

int rdma_conn_p2p::oneside_recv_pre(void *buf, size_t count, non_block_handle *req, oneside_info* my_info){
    req->_lock.release();
    req->_lock.acquire();

    int index = irecv_info_pool.pop();
    irecv_info *irecv_ptr = irecv_info_pool.get(index);
    irecv_ptr->recv_addr = (uintptr_t)buf;
    irecv_ptr->recv_size = count;
    //there the recv_mr must be register
    irecv_ptr->recv_mr   = ibv_reg_mr(recv_rdma_conn.pd, (void*)irecv_ptr->recv_addr, irecv_ptr->recv_size,
                                                 IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
    ASSERT(irecv_ptr->recv_mr);
    irecv_ptr->req_handle  = req;
    req->index = index;
    req->type  = WAIT_ONESIDE_RECV_PRE;
    req->oneside_info_addr = (uintptr_t)my_info;
    my_info->req = req;//bind the non_block_handle

    if(pending_queue.empty()){
        _lock.acquire();
        if(pending_queue.empty()){
            //create a index for current irecv
            irecv_queue.push(index);
        }
        else {
            _lock.release();
            pending_queue_not_empty(buf, count, index, req);
            return 1;
        }
        _lock.release();
        return 1;
    }
    else{
        pending_queue_not_empty(buf, count, index, req);
    }
    return 1;
}

int rdma_conn_p2p::oneside_isend(oneside_info *peer_info, non_block_handle *req){
    req->_lock.release();
    req->_lock.acquire();
    req->oneside_info_addr = (uintptr_t)peer_info;
    req->type = WAIT_ONESIDE_SEND;
    req->index = peer_info->write_index;
    //change the lock 
    isend_info * my_iwrite_info = isend_info_pool.get(peer_info->write_index);
    my_iwrite_info->req_handle = req;

    addr_mr_pair *mr_pair = addr_mr_pool.pop();
    ASSERT(mr_pair);
    mr_pair->send_addr = peer_info->send_buffer;
    mr_pair->send_mr   = peer_info->send_mr;
    ASSERT(mr_pair->send_mr);
    mr_pair->len       = peer_info->send_mr->length;
    mr_pair->isend_index = peer_info->write_index;
    uint32_t imm_data = peer_info->read_index;
    imm_data |= IMM_DATA_MAX_MASK;

    _lock_for_peer_num.acquire();
    if(peer_left_recv_num <= 0){
        unsend_queue.emplace(mr_pair, peer_info->recv_buffer, peer_info->rkey, imm_data);
        _lock_for_peer_num.release();
        return 1;
    }
    peer_left_recv_num--;
    _lock_for_peer_num.release();

    CCALL(pp_post_write(mr_pair, peer_info->recv_buffer, peer_info->rkey, imm_data));
    ITR_SEND("[ONESIDE_MSG sending...] send_addr %llx, len %d, iwrite_index %d, iread_index %d, rkey %d, recv_buffer %llx\n", (long long unsigned int)mr_pair->send_addr, mr_pair->len, mr_pair->isend_index, peer_info->read_index, (int)peer_info->rkey, (long long)peer_info->recv_buffer);
    return 1;
}

//clear the resource(ibv_dereg_mr)
bool rdma_conn_p2p::end_oneside(oneside_info *peer_info){
    if(peer_info->type == ONE_SIDE_SEND){
        isend_info *my_send_info = isend_info_pool.get(peer_info->write_index);
        CCALL(ibv_dereg_mr(my_send_info->send_mr));
        memset(my_send_info, 0, sizeof(isend_info));
        isend_info_pool.push(peer_info->write_index);
    }
    else{
        ASSERT(peer_info->type == ONE_SIDE_RECV);
        irecv_info *my_recv_info = irecv_info_pool.get(peer_info->read_index);
        CCALL(ibv_dereg_mr(my_recv_info->recv_mr));
        memset(my_recv_info, 0, sizeof(irecv_info));
        irecv_info_pool.push(peer_info->read_index);
    }
    return true;
}

int rdma_conn_p2p::hp_isend_init(non_block_handle* req, oneside_info *peer_info)
{
    req->_lock.release();
    req->_lock.acquire();
    int isend_index = isend_info_pool.pop();
    isend_info *isend_ptr = isend_info_pool.get(isend_index);ASSERT(isend_ptr);
    isend_ptr->req_handle = req;
    req->index = isend_index;
    req->type  = WAIT_HP_ISEND_INIT;
    req->oneside_info_addr = (uintptr_t)peer_info;
    ctl_flow_info *ack_ctl_addr = ctl_flow_pool.pop(); ASSERT(ack_ctl_addr);
    struct ibv_mr *mr = ibv_reg_mr(send_rdma_conn.pd, ack_ctl_addr, sizeof(ctl_flow_info), IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
    pp_post_recv(send_rdma_conn.qp, (uintptr_t)ack_ctl_addr, mr->lkey, sizeof(ctl_flow_info), mr);

    send_req_clt_info req_msg;
    req_msg.len = 0; req_msg.send_addr  = (uintptr_t) nullptr; req_msg.send_mr = nullptr;
    isend_ptr->send_mr = req_msg.send_mr;
    req_msg.isend_index = isend_index;
    req_msg.is_oneside  = true;

    _lock_for_peer_num.acquire();
    if(peer_left_recv_num <= 0){
        unsend_queue.emplace(req_msg);
        _lock_for_peer_num.release();
        return 1;
    }
    peer_left_recv_num--;
    _lock_for_peer_num.release();

    pp_post_send(send_rdma_conn.qp, (uintptr_t)&req_msg, 0, sizeof(req_msg), true, false);
    ITR_SEND("hp isend init... isend_index = %d\n", isend_index);
    return 1;
}

int rdma_conn_p2p::hp_irecv_init(void *buf, size_t count, non_block_handle *req, int send_times, oneside_info *my_info){
    req->_lock.release();
    req->_lock.acquire();

    int hp_irecv_index = irecv_info_pool.pop();
    irecv_info *irecv_ptr = irecv_info_pool.get(hp_irecv_index);
    irecv_ptr->recv_addr = (uintptr_t)buf;
    irecv_ptr->recv_size = count;
    //there the recv_mr must be register
    irecv_ptr->recv_mr   = ibv_reg_mr(recv_rdma_conn.pd, (void*)irecv_ptr->recv_addr, irecv_ptr->recv_size,
                                      IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
    ASSERT(irecv_ptr->recv_mr);
    irecv_ptr->req_handle  = req;
    req->index = hp_irecv_index;
    req->type  = WAIT_HP_IRECV_INIT;
    req->oneside_info_addr = (uintptr_t)my_info;
    //set send_times
    req->hp_irecv_times = send_times;
    req->hp_cur_times   = 0;
    my_info->req = req;//bind the non_block_handle


    if(pending_queue.empty()){
        _lock.acquire();
        if(pending_queue.empty()){
            //create a index for current irecv
            irecv_queue.push(hp_irecv_index);
        }
        else {
            _lock.release();
            pending_queue_not_empty(buf, count, hp_irecv_index, req);
            return 1;
        }
        _lock.release();
        return 1;
    }
    else{
        pending_queue_not_empty(buf, count, hp_irecv_index, req);
    }
    return 1;


}

int rdma_conn_p2p::hp_isend(const void *buf, size_t count, non_block_handle *req, oneside_info* peer_info){
    req->_lock.release();
    req->_lock.acquire();

    int hp_isend_index = isend_info_pool.pop();
    isend_info *hp_isend_ptr = isend_info_pool.get(hp_isend_index); ASSERT(hp_isend_ptr);

    req->oneside_info_addr = (uintptr_t)peer_info;
    req->type = WAIT_HP_ISEND;
    ASSERT(peer_info->rkey);ASSERT(peer_info->recv_buffer);
    ASSERT(peer_info->send_mr == nullptr);
    req->index = hp_isend_index;

    addr_mr_pair *mr_pair = addr_mr_pool.pop();ASSERT(mr_pair);
    hp_isend_ptr->send_addr = mr_pair->send_addr = (uintptr_t) const_cast<void*>(buf);
    hp_isend_ptr->send_mr = mr_pair->send_mr   = ibv_reg_mr(send_rdma_conn.pd, const_cast<void*>(buf), count, IBV_ACCESS_LOCAL_WRITE);
    hp_isend_ptr->send_size = count;
    hp_isend_ptr->req_handle = req;
    mr_pair->len = count;
    mr_pair->isend_index = hp_isend_index;
    uint32_t imm_data = peer_info->read_index;
    imm_data |= IMM_DATA_MAX_MASK;

    _lock_for_peer_num.acquire();
    if(peer_left_recv_num <= 0){
        unsend_queue.emplace(mr_pair, peer_info->recv_buffer, peer_info->rkey, imm_data);
        _lock_for_peer_num.release();
        return 1;
    }
    peer_left_recv_num--;
    _lock_for_peer_num.release();

    CCALL(pp_post_write(mr_pair, peer_info->recv_buffer, peer_info->rkey, imm_data));
    ITR_SEND("[hp_isend_msg...] send_addr %llx, len %d, iwrite_index %d, iread_index %d, rkey %d, recv_buffer %llx\n", (long long unsigned int)mr_pair->send_addr, mr_pair->len, mr_pair->isend_index, peer_info->read_index, (int)peer_info->rkey, (long long)peer_info->recv_buffer);
    return 1;
}
bool rdma_conn_p2p::hp_recv_wait(non_block_handle *req, int send_times){
    enum WAIT_TYPE type = req->type;
    if(type == WAIT_HP_IRECV_INIT){
        volatile int cur_recv;
        while((cur_recv = req->hp_cur_times) < send_times){}
        ASSERT(req->hp_cur_times >= send_times);
        req->_lock.acquire();
    }
    req->_lock.release();
    return true;
}
*/
#endif
