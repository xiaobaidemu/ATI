#include "rdma_header.h"
// rdma_fd_data
rdma_listener::rdma_listener(rdma_environment *env, const char* bind_ip, const uint16_t port):
listener(env), _bind_endpoint(bind_ip, port)
{
    listen_type = rdma_fd_data(this);
    CCALL(rdma_create_id(env->env_ec, &listener_rdma_id, &listen_type, RDMA_PS_TCP));
    ASSERT(listener_rdma_id);
    CCALL(rdma_bind_addr(listener_rdma_id, _bind_endpoint.data()));
    _start_accept_required.store(false);
    _rundown.register_callback([&]() {
        if (OnClose) {
            OnClose(this);
        }
        ASSERT_RESULT(listener_rdma_id);
        CCALL(rdma_destroy_id(listener_rdma_id));
        listener_rdma_id = nullptr;
        _close_finished = true;
    });
}
void rdma_listener::process_accept_success(rdma_connection* new_rdma_conn)
{
    ASSERT(new_rdma_conn);
    /*bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        ASSERT(new_rdma_conn->conn_id);
        WARN("rdma_connection(conn_id:%ld) _rundown.try_acquire() failed\n", (uintptr_t)new_rdma_conn->conn_id);
        _rundown.release();
        return;
    }*/
    new_rdma_conn->_status.store(CONNECTION_CONNECTED);
    ASSERT(OnAccept);
    OnAccept(this, new_rdma_conn);
   // _rundown.release();
}
void rdma_listener::process_accept_fail()
{
    /*bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return;
    }*/
    const int error = errno;
    if(OnAcceptError){
        OnAcceptError(this, error);
    }
    //_rundown.release();
}

bool rdma_listener::start_accept()
{
    ASSERT(OnAccept != nullptr);
    bool expect = false;
    if (!_start_accept_required.compare_exchange_strong(expect, true)) {
        return false;
    }

    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return false;
    }

    CCALL(rdma_listen(listener_rdma_id, INT_MAX));
    _rundown.release();
    return true;
}

bool rdma_listener::async_close()
{
    bool need_release;
    if (!_rundown.try_acquire(&need_release)) {
        if (need_release) {
            _rundown.release();
        }
        return false;
    }

    if (!_rundown.shutdown()) {
        _rundown.release();
        return false;
    }
    _rundown.release();
    ((rdma_environment*)_environment)->push_and_trigger_notification(rdma_event_data::rdma_listener_close(this));
    return true;
}
