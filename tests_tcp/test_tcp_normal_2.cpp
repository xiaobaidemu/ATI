
#include <tcp_src/tcp_conn_system.h>
#include <thread>
#include <vector>
#include <sys/sysinfo.h>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
//#define DATA_LEN            (4*1024)
#define ITERS               100

/*
 * test case:
 * 1.isend one small msg: 16, 32, 64, 128, 512
 * 2.isend one big   msg: 1024, 1024*16, 1024*256, 1024*1024, 1024*1024*8
 */
static char *dummy_data_1;
static char *dummy_data_2;

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
    dummy_data_1 = (char*)malloc(DATA_LEN);
    dummy_data_2 = (char*)malloc(DATA_LEN);
    ITR_SPECIAL("~~~~ size %d, unit %c, DATA_LEN %lld~~~~\n", (int)size, unit, (long long)DATA_LEN);
    int threads_num = 2;
    std::vector<std::thread> processes(threads_num);
    for (size_t i = 0; i < DATA_LEN; ++i) {
        dummy_data_1[i] = (char)(unsigned char)i;
        dummy_data_2[i] = (char)(unsigned char)i;
    }
    for(int i = 0;i < threads_num;i++){
        processes[i] = std::thread([i, DATA_LEN](){
            //CCALL(sched_setaffinity(0, sizeof(mask), &mask));
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);
            comm_system comm_object(LOCAL_HOST, LOCAL_PORT + i);
            async_conn_system *sys = comm_object.get_conn_system();
            async_conn_p2p *tcp_conn_object = sys->init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(tcp_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            char *recv_buf_1 = (char*)malloc(DATA_LEN);
            char *recv_buf_2 = (char*)malloc(DATA_LEN);
            timer _timer;
            non_block_handle isend_req_1, isend_req_2,irecv_req_1, irecv_req_2;
            for(int iter = 0; iter < ITERS; iter++){
                tcp_conn_object->isend(dummy_data_1, DATA_LEN, &isend_req_1);
                tcp_conn_object->isend(dummy_data_2, DATA_LEN, &isend_req_2);
                tcp_conn_object->irecv(recv_buf_1, DATA_LEN, &irecv_req_1);
                tcp_conn_object->irecv(recv_buf_2, DATA_LEN, &irecv_req_2);
                tcp_conn_object->wait(&isend_req_1);
                tcp_conn_object->wait(&isend_req_2);
                tcp_conn_object->wait(&irecv_req_1);
                tcp_conn_object->wait(&irecv_req_2);
                ASSERT(memcmp(dummy_data_1, recv_buf_1, DATA_LEN) == 0);
                ASSERT(memcmp(dummy_data_2, recv_buf_2, DATA_LEN) == 0);
            }
            double time_consume = _timer.elapsed();
            size_t total_size = DATA_LEN*4*ITERS;
            double speed = (double)total_size/1024/1024/time_consume;

            SUCC("time %.6lfs, total_size %lld bytes, speed %.2lf MB/sec\n", time_consume, (long long)total_size, speed);
            //sleep(1);
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


