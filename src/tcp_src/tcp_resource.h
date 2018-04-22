#ifndef SENDRECV_TCP_RESOURCE_H
#define SENDRECV_TCP_RESOURCE_H
#include <common/common.h>

enum tcp_head_type{
    HEAD_TYPE_INVAILD = 0,
    HEAD_TYPE_INIT,
    HEAD_TYPE_SEND,
    HEAD_TYPE_FINALIZE,
};

struct datahead{
    enum tcp_head_type type;
    size_t content_size;
    char ip[16];//my_listen_ip
    int  port;//my_listen_port
public:
    datahead(){}
    datahead(enum tcp_head_type type, size_t size)
            :type(type), content_size(size){}
};

struct data_state{
    size_t      recvd_head_size;
    datahead    head_msg;
    size_t      total_content_size;
    size_t      recvd_content_size;
    char*       content;
public:
    data_state()
            :recvd_head_size(0), head_msg(), total_content_size(0), recvd_content_size(0),content(nullptr) {}
    void clear(){
        recvd_head_size = 0;
        memset(&head_msg, 0, sizeof(datahead));
        total_content_size = 0;
        recvd_content_size = 0;
        content = nullptr;
    }
};

struct handler{
    bool   is_finish;
    lock _lock;
public:
    handler():is_finish(false){}
    void set_handler() {
            is_finish = false;
    }

};


#endif //SENDRECV_TCP_RESOURCE_H
