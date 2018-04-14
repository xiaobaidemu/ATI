#include <rdma_src/conn_system.h>
#include <thread>
#include <vector>
#include <sys/sysinfo.h>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
//#define DATA_LEN            (4*1024)
#define ITERS               1000

static char *dummy_data;
static lock ctl_lock;
int main(int argc, char *argv[])
{
    if(argc < 3){
        ERROR("two few parameter.\n");
        exit(0);
    }
    size_t size = (size_t)atol(argv[1]);
    char   unit = argv[2][0];
    size_t DATA_LEN;
    switch(unit){
        case 'b':
            DATA_LEN = size;
            break;
        case 'k':
            DATA_LEN = size * 1024;
            break;
        case 'm':
            DATA_LEN = size * 1024 *1024;
            break;
    }
    dummy_data = (char*)malloc(DATA_LEN);
    int threads_num = 2;
    std::vector<std::thread> processes(threads_num);
    for (size_t i = 0; i < DATA_LEN; ++i) {
        dummy_data[i] = (char)(unsigned char)i;
    }
    ctl_lock.acquire();
    for(int i = 0;i < threads_num;i++){
        processes[i] = std::thread([i, DATA_LEN](){
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);
            conn_system sys("127.0.0.1", LOCAL_PORT+i);
            rdma_conn_p2p *rdma_conn_object = sys.init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(rdma_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            char *recv_buf = (char*)malloc(DATA_LEN);

            //const void *buf, size_t count, non_block_handle *req, oneside_info *peer_info
            non_block_handle oneside_send_pre_req, oneside_recv_req;
            oneside_info send_info, recv_info;
            if(i == 0){
                rdma_conn_object->oneside_send_pre(dummy_data, DATA_LEN, &oneside_send_pre_req, &send_info);
                rdma_conn_object->wait(&oneside_send_pre_req);
                SUCC("[Finished SEND oneside exchange info] recv_buffer %llx, rkey %d\n",
                     (long long)send_info.recv_buffer, (int)send_info.rkey);
            }
            else{
                rdma_conn_object->oneside_recv_pre(recv_buf, DATA_LEN, &oneside_recv_req, &recv_info);
                rdma_conn_object->wait(&oneside_recv_req);
                SUCC("[Finished RECV oneside exchange info] recv_buffer %llx, rkey %d\n",
                     (long long)recv_info.recv_buffer, (int)recv_info.rkey);
            }
            timer _timer;
            non_block_handle iwrite_req;
            for(int iter = 0; iter < ITERS; iter++){
                if(i == 0){
                    //ITR_SPECIAL("oneside_isend begin %d time.\n", iter);
                    rdma_conn_object->oneside_isend(&send_info, &iwrite_req);
                    rdma_conn_object->wait(&iwrite_req);
                    //ITR_SPECIAL("oneside_isend end %d time.\n", iter);
                }
            }
            if(i == 0)
            {
                double time_consume = _timer.elapsed();
                size_t total_size = DATA_LEN*ITERS;
                double speed = (double)total_size/1024/1024/time_consume;
                SUCC("time %.6lfs, total_size %lld bytes, speed %.2lf MB/sec\n", time_consume,
                     (long long)total_size, speed);
            }
            if(i == 0)
                ctl_lock.release();
            else
                ctl_lock.acquire();
            if(i == 0)rdma_conn_object->end_oneside(&send_info);
            else rdma_conn_object->end_oneside(&recv_info);
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


