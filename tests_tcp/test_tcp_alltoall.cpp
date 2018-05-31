#include <at_sendrecv.h>
#include <thread>
#include <vector>
#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)
#define DATA_LENGTH    (1024 * 1024)

int main(int argc, char **argv){
    int process_num = 2;//default
    if(argc > 1)
        process_num = atoi(argv[1]);
    SUCC("READY to START %d process.\n", process_num);

    std::vector<std::thread> processes(process_num);

    for(int i = 0;i < process_num;i++){
        processes[i] = std::thread([i, process_num](){
            char* send_data = (char*)malloc(DATA_LENGTH);
            for (size_t i = 0; i < DATA_LENGTH; ++i) {
                send_data[i] = (char)(unsigned char)i;
            }
            char* recv_data = (char*)malloc(DATA_LENGTH * process_num);

            comm_system sys(LOCAL_HOST, LOCAL_PORT + i);
            async_conn_system *comm_object = sys.get_conn_system();
            std::vector<async_conn_p2p*> comm_list(process_num);
            for(int k = 0;k < process_num;k++){
                if(k != i) {
                    async_conn_p2p *tcp_conn_object = comm_object->init(PEER_HOST, PEER_PORT_BASE + k);
                    comm_list[k] = tcp_conn_object;
                    ASSERT(tcp_conn_object);
                }
                else comm_list[k] = nullptr;
            }
            std::vector<non_block_handle> send_handlers(process_num);
            std::vector<non_block_handle> recv_handlers(process_num);
            for(int iter = 0; iter < 2500; iter++) {

                for (int des = 0; des < process_num; ++des) {
                    if (des != i) {
                        comm_list[des]->isend(send_data, DATA_LENGTH, &send_handlers[des]);
                    }

                }
                for (int src = 0; src < process_num; ++src) {
                    if (src != i) {
                        comm_list[src]->irecv(recv_data + src * DATA_LENGTH, DATA_LENGTH, &recv_handlers[src]);
                    }
                }

                for (int des = 0; des < process_num; ++des) {
                    if (des != i)
                        comm_list[des]->wait(&send_handlers[des]);
                }
                for (int src = 0; src < process_num; ++src) {
                    if (src != i) {
                        comm_list[src]->wait(&recv_handlers[src]);
                        ASSERT(memcmp(send_data, recv_data + src * DATA_LENGTH, DATA_LENGTH) == 0);
                    }
                }
            }
            WARN("==========%s_%d   finish task.\n", LOCAL_HOST, LOCAL_PORT + i);
            SUCC("[%s:%d] has succeed initing with all other %d process.\n",
                 LOCAL_HOST, LOCAL_PORT + i, process_num - 1);
#ifdef IBEXIST
            WARN("++++++ the IBEXIST macro ++++++\n");
#endif
        });
    }
    for(auto& t: processes)
        t.join();
    return 0;
}