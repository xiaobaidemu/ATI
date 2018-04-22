
#include <tcp_src/tcp_conn_system.h>
#include <thread>
#include <vector>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
//#define DATA_LEN            (512*1024)
#define ITERS               (100)

/*
 * test case:
 * 1.isend one small msg: 16, 32, 64, 128, 512
 * 2.isend one big   msg: 1024, 1024*16, 1024*256, 1024*1024, 1024*1024*8
 */
static char *dummy_data;

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
            DATA_LEN = size * 1024 * 1024;
            break;
    }
    dummy_data = (char*)malloc(DATA_LEN);
    ITR_SPECIAL("~~~~ size %d, unit %c, DATA_LEN %lld~~~~\n", (int)size, unit, (long long)DATA_LEN);
    int threads_num = 2;
    std::vector<std::thread> processes(threads_num);

    for(int i = 0;i < threads_num;i++){
        processes[i] = std::thread([i, DATA_LEN](){
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);
            tcp_conn_system sys("127.0.0.1", LOCAL_PORT+i);
            tcp_conn_p2p *tcp_conn_object = sys.init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(tcp_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            char *recv_buf[ITERS];
            for(int j = 0;j < ITERS;j++){
                recv_buf[j] = (char*)malloc(DATA_LEN);
            }
            for (int i = 0; i < DATA_LEN; ++i) {
                dummy_data[i] = (char)(unsigned char)(i+1);
            }
            non_block_handle isend_req_array[ITERS];
            non_block_handle irecv_req_array[ITERS];
            timer _timer;

            for(int iter = 0; iter < ITERS; iter++){
                if(i == 0){
                    tcp_conn_object->isend(dummy_data, DATA_LEN, isend_req_array + iter);
                    WARN("[rank %d] isend %d times.\n", i, iter);
                }
                else{
                    tcp_conn_object->irecv(recv_buf[iter], DATA_LEN, irecv_req_array + iter);
                    //SUCC("[rank %d] irecv %d times.\n", i, iter);
                }
            }
            if(i == 0)
                tcp_conn_object->wait(isend_req_array + ITERS -1);
            else
                tcp_conn_object->wait(irecv_req_array + ITERS -1);
            if(i == 1)
                ASSERT(memcmp(dummy_data, recv_buf[ITERS -1], 0) == 0);

            double time_consume = _timer.elapsed();
            size_t total_size = (long long)DATA_LEN*ITERS;
            double speed = (double)total_size/1024/1024/time_consume;
            //SUCC("time %.6lfs, total_size %lld bytes, speed %.2lf MB/sec\n", time_consume, (long long)total_size, speed);
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


