#include <rdma_src/conn_system.h>
#include <thread>
#include <vector>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
#define DATA_LEN            (128)

/*
 * test case:
 * 1.isend one small msg: 16, 32, 64, 128, 512
 * 2.isend one big   msg: 1024, 1024*16, 1024*256, 1024*1024, 1024*1024*8
 */
static char dummy_data[DATA_LEN];

int main()
{
    int threads_num = 2;
    std::vector<std::thread> processes(threads_num);
    for (int i = 0; i < DATA_LEN; ++i) {
        dummy_data[i] = (char)(unsigned char)i;
    }
    for(int i = 0;i < threads_num;i++){
        processes[i] = std::thread([i](){
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);
            conn_system sys("127.0.0.1", LOCAL_PORT+i);
            rdma_conn_p2p *rdma_conn_object = sys.init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(rdma_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            char *recv_buf = (char*)malloc(DATA_LEN);
            if(i == 0){
                non_block_handle isend_req;
                rdma_conn_object->isend(dummy_data, DATA_LEN, &isend_req);
                rdma_conn_object->wait(&isend_req);
            }
            else{
                non_block_handle irecv_req;
                rdma_conn_object->irecv(recv_buf, DATA_LEN, &irecv_req);
                rdma_conn_object->wait(&irecv_req);
            }

        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


