#include <rdma_src/conn_system.h>
#include <thread>
#include <vector>
#include <sys/sysinfo.h>

#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
#define SMALL_DATA_LEN_BASE    (1024)
#define BIG_DATA_LEN_BASE      (1024*1024*2)
#define ONE_SIDE_BASE          (1024*256)
#define ITERS               1000

//mix oneside communication with normal isend- irecv

//isendrecv -- oneside - isendrecv oneside oneside oneside
static int random_array[ITERS];
static int multiple_array[ITERS];
static bool direction[ITERS];

//0:small data 1:big data 2:oneside
int main(int argc, char *argv[])
{
    int small = 0, big = 0, one_side = 0;
    srand(0xeaddbeaf);
    size_t total_size = 0;
    int big_send_cnt = 0, small_send_cnt = 0;
    for (int i = 0;i < ITERS;++i){
        random_array[i] = rand()%3;
        if(random_array[i] == 0){
            multiple_array[i] = rand()%1024 + 1;
            small++;
        }
        else if(random_array[i] == 1){
            multiple_array[i] = rand()%4 + 1;
            big++;
        }
        else{
            multiple_array[i] = rand()%16 + 1;
            one_side++;
        }
        direction[i] = rand()%2;
        //true: i==0 send ;i ==1 recv   false:i==0 recv;i ==1 send
    }
    SUCC("small %d, big %d, one_side %d.\n", small, big, one_side);
    int threads_num = 2;
    std::vector<std::thread> processes(threads_num);
    for(int i = 0;i < threads_num;i++){
        processes[i] = std::thread([i](){
            WARN("%s:%d ready to init with %s:%d.\n", LOCAL_HOST, LOCAL_PORT+i,
                 PEER_HOST, PEER_PORT_BASE + (i+1)%2);
            conn_system sys("127.0.0.1", LOCAL_PORT+i);
            rdma_conn_p2p *rdma_conn_object = sys.init("127.0.0.1", PEER_PORT_BASE + (i+1)%2);
            ASSERT(rdma_conn_object);
            WARN("%s:%d init finished.\n", LOCAL_HOST, LOCAL_PORT+i);
            char* send_buffer, *recv_buffer;
            for(int iter = 0;iter < ITERS;iter++){
                if(random_array[iter] == 0){
                    size_t data_len = SMALL_DATA_LEN_BASE * multiple_array[iter];
                    if(i == 0){
                        if(direction[iter]){
                            non_block_handle small_isend_req;
                            send_buffer = (char*)malloc(data_len);
                            rdma_conn_object->isend(send_buffer, data_len, &small_isend_req);
                            rdma_conn_object->wait(&small_isend_req);
                            free(send_buffer);
                            RANK_0("[iter %d] finished ISEND small_data %lld.\n", iter, (long long)data_len);
                        }
                        else{
                            recv_buffer = (char*)malloc(data_len);
                            non_block_handle small_irecv_req;
                            rdma_conn_object->irecv(recv_buffer, data_len, &small_irecv_req);
                            rdma_conn_object->wait(&small_irecv_req);
                            free(recv_buffer);
                            RANK_0("[iter %d] finished IRECV small_data %lld.\n", iter, (long long)data_len);
                        }
                    }
                    else{
                        if(direction[iter]){
                            recv_buffer = (char*)malloc(data_len);
                            non_block_handle small_irecv_req;
                            rdma_conn_object->irecv(recv_buffer, data_len, &small_irecv_req);
                            rdma_conn_object->wait(&small_irecv_req);
                            free(recv_buffer);
                            RANK_1("[iter %d] finished IRECV small_data %lld.\n", iter, (long long)data_len);
                        }
                        else{
                            send_buffer = (char*)malloc(data_len);
                            non_block_handle small_isend_req;
                            rdma_conn_object->isend(send_buffer, data_len, &small_isend_req);
                            rdma_conn_object->wait(&small_isend_req);
                            free(send_buffer);
                            RANK_1("[iter %d] finished ISEND small_data %lld.\n", iter, (long long)data_len);
                        }
                    }
                }
                else if(random_array[iter] == 1){
                    size_t data_len = BIG_DATA_LEN_BASE * multiple_array[iter];
                    if(i == 0){
                        if(direction[iter]){
                            non_block_handle big_isend_req;
                            send_buffer = (char*)malloc(data_len);
                            rdma_conn_object->isend(send_buffer, data_len, &big_isend_req);
                            rdma_conn_object->wait(&big_isend_req);
                            free(send_buffer);
                            RANK_0("[iter %d] finished ISEND big_data %lld.\n", iter, (long long)data_len);
                        }
                        else{
                            recv_buffer = (char*)malloc(data_len);
                            non_block_handle big_irecv_req;
                            rdma_conn_object->irecv(recv_buffer, data_len, &big_irecv_req);
                            rdma_conn_object->wait(&big_irecv_req);
                            free(recv_buffer);
                            RANK_0("[iter %d] finished IRECV big_data %lld.\n", iter, (long long)data_len);
                        }
                    }
                    else{
                        ASSERT(i == 1);
                        if(direction[iter]){
                            recv_buffer = (char*)malloc(data_len);
                            non_block_handle big_irecv_req;
                            rdma_conn_object->irecv(recv_buffer, data_len, &big_irecv_req);
                            rdma_conn_object->wait(&big_irecv_req);
                            free(recv_buffer);
                            RANK_1("[iter %d] finished IRECV big_data %lld.\n", iter, (long long)data_len);
                        }
                        else{
                            send_buffer = (char*)malloc(data_len);
                            non_block_handle big_isend_req;
                            rdma_conn_object->isend(send_buffer, data_len, &big_isend_req);
                            rdma_conn_object->wait(&big_isend_req);
                            free(send_buffer);
                            RANK_1("[iter %d] finished ISEND big_data %lld.\n", iter, (long long)data_len);
                        }
                    }
                }
                else{
                    ASSERT(random_array[iter] == 2);
                    size_t data_len = ONE_SIDE_BASE * multiple_array[iter];
                    if(i == 0){
                        if(direction[iter]){
                            send_buffer = (char*)malloc(data_len);
                            non_block_handle oneside_send_pre_req, iwrite_req;
                            oneside_info send_info;
                            rdma_conn_object->oneside_send_pre(send_buffer, data_len, &oneside_send_pre_req, &send_info);
                            rdma_conn_object->wait(&oneside_send_pre_req);
                            rdma_conn_object->oneside_isend(&send_info, &iwrite_req);
                            rdma_conn_object->wait(&iwrite_req);
                            free(send_buffer);
                            RANK_0("[iter %d] finished ONE_SIDE_WRITE %lld.\n", iter, (long long)data_len);

                        }
                        else{
                            recv_buffer = (char*)malloc(data_len);
                            non_block_handle  oneside_recv_req;
                            oneside_info recv_info;
                            rdma_conn_object->oneside_recv_pre(recv_buffer, data_len, &oneside_recv_req, &recv_info);
                            rdma_conn_object->wait(&oneside_recv_req);
                            RANK_0("[iter %d] finishing ONE_SIDE_READ %lld.\n", iter, (long long)data_len);
                        }
                    }
                    else{
                        if(direction[iter]){
                            recv_buffer = (char*)malloc(data_len);
                            non_block_handle  oneside_recv_req;
                            oneside_info recv_info;
                            rdma_conn_object->oneside_recv_pre(recv_buffer, data_len, &oneside_recv_req, &recv_info);
                            rdma_conn_object->wait(&oneside_recv_req);
                            RANK_1("[iter %d] finishing ONE_SIDE_READ %lld.\n", iter, (long long)data_len);
                        }
                        else{
                            send_buffer = (char*)malloc(data_len);
                            non_block_handle oneside_send_pre_req, iwrite_req;
                            oneside_info send_info;
                            rdma_conn_object->oneside_send_pre(send_buffer, data_len, &oneside_send_pre_req, &send_info);
                            rdma_conn_object->wait(&oneside_send_pre_req);
                            rdma_conn_object->oneside_isend(&send_info, &iwrite_req);
                            rdma_conn_object->wait(&iwrite_req);
                            free(send_buffer);
                            RANK_1("[iter %d] finished ONE_SIDE_WRITE %lld.\n", iter, (long long)data_len);
                        }

                    }
                }
            }
            sleep(5);
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}


