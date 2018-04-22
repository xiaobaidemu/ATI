#include <tcp_src/tcp_conn_system.h>
#include <thread>
#include <vector>
#include <sys/sysinfo.h>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
#define ITERS               1000
#define BASE_LEN            1024

static int multiple_array[ITERS];

int main(int argc, char *argv[])
{
    for (int i = 0;i < ITERS;++i){
        multiple_array[i] = rand()%1024 +1;
    }
    int threads_num = 2;
    std::vector<std::thread> processes(threads_num);
    for(int i = 0;i < threads_num;i++){
        processes[i] = std::thread([i](){
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);
            tcp_conn_system sys("127.0.0.1", LOCAL_PORT+i);
            tcp_conn_p2p *tcp_conn_object = sys.init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(tcp_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            non_block_handle isend_req, irecv_req;
            for(int iter = 0; iter < ITERS; iter++){
                char* send_buf = (char*)malloc(multiple_array[iter] * BASE_LEN);
                char* recv_buf = (char*)malloc(multiple_array[iter] * BASE_LEN);
                for (size_t i = 0; i < multiple_array[iter] * BASE_LEN; ++i) {
                    send_buf[i] = (char)(unsigned char)i;
                }
                tcp_conn_object->isend(send_buf, multiple_array[iter] * BASE_LEN, &isend_req);
                tcp_conn_object->irecv(recv_buf,multiple_array[iter] * BASE_LEN, &irecv_req);
                tcp_conn_object->wait(&isend_req);
                tcp_conn_object->wait(&irecv_req);
                ASSERT(memcmp(send_buf, recv_buf, multiple_array[iter] * BASE_LEN) == 0);
                free(send_buf);
                free(recv_buf);
            }
            SUCC("=========FINISH TASK=========\n");
            sleep(1);
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


