

#include <at_sendrecv.h>
#include <thread>
#include <vector>
#include <sys/sysinfo.h>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
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
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);

            comm_system sys("127.0.0.1", LOCAL_PORT+i);
            async_conn_system *comm_object = sys.get_conn_system();

            async_conn_p2p *async_conn_object = comm_object->init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(async_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            char *recv_buf = (char*)malloc(DATA_LEN);
            timer _timer;
            non_block_handle isend_req, irecv_req;
            for(int iter = 0; iter < ITERS; iter++){
                if(i == 0){
                    if(iter == (int)ITERS/2)
                    {
                        ERROR("[rank %d] iter %d  break!!!!!!!!!!.\n", i , iter);
                        return;
                    }
                }
                async_conn_object->isend(dummy_data, DATA_LEN, &isend_req);
                async_conn_object->irecv(recv_buf, DATA_LEN, &irecv_req);
                async_conn_object->wait(&isend_req);
                async_conn_object->wait(&irecv_req);
                //ASSERT(memcmp(dummy_data, recv_buf, DATA_LEN) == 0);
            }
            double time_consume = _timer.elapsed();
            size_t total_size = DATA_LEN*2*ITERS;
            double speed = (double)total_size/1024/1024/time_consume;

            SUCC("time %.6lfs, total_size %lld bytes, speed %.2lf MB/sec\n", time_consume, (long long)total_size, speed);
#ifdef IBEXIST
            SUCC("IBEXIST.\n");
#endif
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


