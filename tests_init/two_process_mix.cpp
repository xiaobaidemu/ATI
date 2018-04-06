#include <rdma_src/conn_system.h>
#include <thread>
#include <vector>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
#define SMALL_DATA_LEN      (256)
#define BIG_DATA_LEN        (1024*1024*4)
#define ITERS               (10)

/*
 * test case:
 * 1.isend one small msg: 16, 32, 64, 128, 512
 * 2.isend one big   msg: 1024, 1024*16, 1024*256, 1024*1024, 1024*1024*8
 */
static char dummy_small_data[SMALL_DATA_LEN];
static char dummy_big_data[BIG_DATA_LEN];
static bool random_array[ITERS];

int main()
{
    int threads_num = 2;
    std::vector<std::thread> processes(threads_num);
    for (int i = 0; i < SMALL_DATA_LEN; ++i) {
        dummy_small_data[i] = (char)(unsigned char)i;
    }
    for (int i = 0; i < BIG_DATA_LEN; ++i) {
        dummy_big_data[i] = (char)(unsigned char)i;
    }
    srand(0xdeadbeef);
    size_t total_size = 0;
    for (int i = 0;i < ITERS;++i){
        random_array[i] = rand()%2;
        if(random_array[i])
            total_size += BIG_DATA_LEN;
        else
            total_size += SMALL_DATA_LEN;
    }
    for(int i = 0;i < threads_num;i++){
        processes[i] = std::thread([i](){
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);
            conn_system sys("127.0.0.1", LOCAL_PORT+i);
            rdma_conn_p2p *rdma_conn_object = sys.init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(rdma_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            char *recv_small_buf = (char*)malloc(SMALL_DATA_LEN);
            char *recv_big_buf = (char*)malloc(BIG_DATA_LEN);
            timer _timer;
            non_block_handle isend_req, irecv_req;
            char* send_data, *recv_data;size_t data_size;
            for(int iter = 0; iter < ITERS;i++){
                if(random_array[i]){
                    send_data = dummy_big_data;
                    recv_data = recv_big_buf;
                    data_size = BIG_DATA_LEN;
                }
                else{
                    send_data = dummy_small_data;
                    recv_data = recv_small_buf;
                    data_size = SMALL_DATA_LEN;
                }
                rdma_conn_object->isend(send_data, data_size, &isend_req);
                rdma_conn_object->irecv(recv_data, data_size, &irecv_req);
                rdma_conn_object->wait(&isend_req);
                rdma_conn_object->wait(&irecv_req);
                ASSERT(memcpy(send_data, recv_data,data_size) == 0);

            }
            double time_consume = _timer.elapsed();
            total_size = total_size*2/1024/1024;
            double speed = (double)total_size/time_consume;
            SUCC("time %.2lfs, total_size %lldMB, speed %.2lf MB/sec\n", time_consume, total_size, speed);
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


