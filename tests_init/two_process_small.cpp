#include <rdma_src/conn_system.h>
#include <thread>
#include <vector>
#include <sys/sysinfo.h>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
//#define DATA_LEN            (4*1024)
#define ITERS               10

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
            DATA_LEN = size * 1024 *1024;
            break; 
    }
    dummy_data = (char*)malloc(DATA_LEN);
    ITR_SPECIAL("~~~~ size %d, unit %c, DATA_LEN %lld~~~~\n", (int)size, unit, (long long)DATA_LEN);
    int threads_num = 2;
    std::vector<std::thread> processes(threads_num);
    for (size_t i = 0; i < DATA_LEN; ++i) {
        dummy_data[i] = (char)(unsigned char)i;
    }
    for(int i = 0;i < threads_num;i++){
        processes[i] = std::thread([i, DATA_LEN](){
            cpu_set_t mask;
            CPU_ZERO(&mask);
            for (int ii = 0; ii < 14; ++ii)
                CPU_SET(ii, &mask), CPU_SET(ii + 28, &mask);
            CCALL(sched_setaffinity(0, sizeof(mask), &mask));
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);
            conn_system sys("127.0.0.1", LOCAL_PORT+i);
            rdma_conn_p2p *rdma_conn_object = sys.init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(rdma_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            char *recv_buf = (char*)malloc(DATA_LEN);
            timer _timer;
            non_block_handle isend_req, irecv_req;
            for(int iter = 0; iter < ITERS; iter++){
                rdma_conn_object->isend(dummy_data, DATA_LEN, &isend_req);
                rdma_conn_object->irecv(recv_buf, DATA_LEN, &irecv_req);
                rdma_conn_object->wait(&isend_req);
                rdma_conn_object->wait(&irecv_req);
                //ASSERT(memcmp(dummy_data, recv_buf, DATA_LEN) == 0);
            }
            double time_consume = _timer.elapsed();
            size_t total_size = DATA_LEN*2*ITERS;
            double speed = (double)total_size/1024/1024/time_consume;
            SUCC("time %.6lfs, total_size %lld bytes, speed %.2lf MB/sec\n", time_consume, (long long)total_size, speed);
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


