#include <rdma_src/conn_system.h>
#include <thread>
#include <vector>
#define LOCAL_HOST          ("127.0.0.1")
#define PEER_HOST           ("127.0.0.1")
#define LOCAL_PORT          (8801)
#define PEER_PORT_BASE      (8801)

int main(int argc, char **argv)
{
    int process_num = 2;//default
    if(argc > 1)
        process_num = atoi(argv[1]);
    SUCC("READY to START %d process.\n", process_num);

    std::vector<std::thread> processes(process_num);

    for(int i = 0;i < process_num;i++){
        processes[i] = std::thread([i, process_num](){
            conn_system sys(LOCAL_HOST, LOCAL_PORT + i);
            for(int k = 0;k < process_num;k++){
                if(k != i) {
                    rdma_conn_p2p *rdma_conn_object = sys.init(PEER_HOST, PEER_PORT_BASE + k);
                    ASSERT(rdma_conn_object);
                }
            }
            SUCC("[%s:%d] has succeed initing with all other %d process.\n",
                 LOCAL_HOST, LOCAL_PORT + i, process_num - 1);
        });
    }

    for(auto& t: processes)
        t.join();
    return 0;
}


