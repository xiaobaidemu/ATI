//
// Created by heboxin on 18-4-15.
//

#include <rdma_src/rdma_resource.h>
#include "tcp_conn_p2p.h"
tcp_conn_p2p::tcp_conn_p2p(){
    _active_connected.acquire();
    _passive_connected.acquire();
}

int tcp_conn_p2p::isend(const void *buf, size_t count, non_block_handle *req)
{
    req->_lock.release();
    req->_lock.acquire();
    req->type = WAIT_ISEND;
    ASSERT(send_socket_conn);
    send_socket_conn->sending_data_queue.push(req);
    datahead* head_ctx = send_socket_conn->datahead_pool.pop();
    head_ctx->content_size = count;
    head_ctx->type         = HEAD_TYPE_SEND;
    std::vector<fragment> pending_send_ctx;
    pending_send_ctx.emplace_back(head_ctx, sizeof(datahead));
    pending_send_ctx.emplace_back(buf, count);
    ASSERT(send_socket_conn->async_send_many(pending_send_ctx));
    return 1;
}
int tcp_conn_p2p::irecv(void *buf, size_t count, non_block_handle *req)
{
    req->tcp_req_info.tcp_irecv_addr = buf;
    req->_lock.release();
    req->_lock.acquire();
    req->type = WAIT_IRECV;
    ASSERT(recv_socket_conn);
    recv_socket_conn->recving_data_queue.push(req);
    return 1;
}
bool tcp_conn_p2p::wait(non_block_handle* req){
    if(req->type == WAIT_ISEND){
        req->_lock.acquire();
        IDEBUG("one ISEND end.\n");
        req->_lock.release();
        return true;
    }
    else if(req->type == WAIT_IRECV){
        if(req->_lock.try_acquire())
        {
            IDEBUG("one IRECV end.\n");
            return true;
        }
        while(true){
            non_block_handle *tmp_req;
            bool success = recv_socket_conn->recving_data_queue.try_pop(&tmp_req);
            ASSERT(success);
            data_state tmp_recvd_data;
            while(!(recv_socket_conn->recvd_data_queue.try_pop(&tmp_recvd_data))){}
            ASSERT(tmp_req->tcp_req_info.tcp_irecv_addr);
            ASSERT(tmp_recvd_data.content);
            memcpy(tmp_req->tcp_req_info.tcp_irecv_addr, tmp_recvd_data.content, tmp_recvd_data.total_content_size);
            tmp_req->tcp_req_info.real_recv_size = tmp_recvd_data.total_content_size;
            tmp_req->_lock.release();
            if(tmp_req == req){
                return true;
                IDEBUG("one IRECV end ======== .\n");
            }
        }
    }
}