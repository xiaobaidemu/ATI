#include <rdma_src/conn_system.h>
#include <iostream>
#include <sstream>
#include <fstream>
#define PX 3
#define PY 3
#define NRANK 9
#define DIRECTION 2
#define L 0
#define R 1
#define T 2
//current only for two node test

typedef  struct nodeinfo{
    char ip_addr[16];
    int  listen_port;
public:
    nodeinfo(){}
    nodeinfo(std::string ip, int port){
        strcpy(ip_addr, ip.c_str());
        listen_port = port;
    }
}nodeinfo;

int myrank;
int allsize;
int iters;
std::vector<nodeinfo> nodelist;
size_t send_bytes;

long long get_curtime(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000000 + tv.tv_usec;
}
void read_host_file(char* file)
{
    std::ifstream fin(file, std::ios::in);
    char line[1024]={0};
    while(fin.getline(line, sizeof(line)))
    {
        std::string lis_ip; int lis_port;
        std::stringstream word(line);
        word >> lis_ip;
        word >> lis_port;
        nodelist.emplace_back(lis_ip, lis_port);
    }
    fin.close();
    int i = 0;
    for(auto &node:nodelist){
        //SUCC("rank:%d ip_addr:%s port:%d\n", i, nodelist[i].ip_addr, nodelist[i].listen_port);
        i++;
    }
    allsize = nodelist.size();
}
// ./socket_isendrecv_speed -i 0 -l host_file (means the index i is belongs to the host_file)
int main(int argc, char* argv[])
{
    char nodelist_file_path[256];
    if(argc < 7){
        ERROR("error parameter only %d parameters.\n", argc);
        exit(0);
    }
    int op;
    while ((op = getopt(argc, argv, "i:f:b:k:m:t:")) != -1){
        switch(op){
            case 'i':
                myrank = atoi(optarg);
                break;
            case 'f':
                strcpy(nodelist_file_path, optarg);
                ITRACE("nodelist_file_path is [%s]\n", nodelist_file_path);
                read_host_file(nodelist_file_path);
                break;
            case 'b':
                send_bytes = atoi(optarg);
                ITRACE("Isend & irecv %lld bytes data.\n", (long long)send_bytes);
                break;
            case 'k':
                send_bytes =  (size_t)1024 * atoi(optarg);
                ITRACE("Isend & irecv %lld bytes data.\n", (long long)send_bytes);
                break;
            case 'm':
                send_bytes =  (size_t)1024 * 1024 * atoi(optarg);
                ITRACE("Isend & irecv %lld bytes data.\n", (long long)send_bytes);
                break;
            case 't':
                iters = atoi(optarg);
                break;
            default:
                ERROR("parameter is error.\n");
                return 0;
        }
    }
    char* dummy_data[T], *recv_buf[T];
    for(int i = 0;i < T;i++){
        dummy_data[i] = (char*)malloc(send_bytes);
        recv_buf[i] = (char*)malloc(send_bytes);
    }
    for(int j = 0;j < T;j++)
        for (size_t i = 0; i < send_bytes; ++i) {
            dummy_data[j][i] = (char)(unsigned char)i;
        }

    std::vector<rdma_conn_p2p*> conn_list(T);
    conn_system sys(nodelist[myrank].ip_addr, nodelist[myrank].listen_port);

    int left  = (myrank-1+allsize)%allsize;
    int right = (myrank+1)%allsize;
    int maxrank = allsize-1;


    if(myrank % 2==0 && myrank != maxrank){
        conn_list[R] = (rdma_conn_p2p*)sys.init(nodelist[right].ip_addr, nodelist[right].listen_port);
    }
    else if(myrank % 2 != 0){
        conn_list[L] = (rdma_conn_p2p*)sys.init(nodelist[left].ip_addr, nodelist[left].listen_port);
    }

    if(myrank % 2!=0){
        conn_list[R] = (rdma_conn_p2p*)sys.init(nodelist[right].ip_addr, nodelist[right].listen_port);
    }
    else if(myrank % 2==0 && myrank != 0){
        conn_list[L] = (rdma_conn_p2p*)sys.init(nodelist[left].ip_addr, nodelist[left].listen_port);
    }

    if(myrank==0){
        conn_list[L] = (rdma_conn_p2p*)sys.init(nodelist[left].ip_addr, nodelist[left].listen_port);
    }
    if(myrank==maxrank){
        conn_list[R] = (rdma_conn_p2p*)sys.init(nodelist[right].ip_addr, nodelist[right].listen_port);
    }

    ASSERT(conn_list[L]);
    ASSERT(conn_list[R]);

    SUCC("%s:%d init finished.\n", nodelist[myrank].ip_addr, nodelist[myrank].listen_port);


    non_block_handle isend_req[T];
    non_block_handle irecv_req[T];
    timer _timer;

    for(int iter = 0; iter < iters; iter++){
        conn_list[L]->isend(dummy_data[L], send_bytes, isend_req+L);
        conn_list[R]->isend(dummy_data[R], send_bytes, isend_req+R);

        conn_list[L]->irecv(recv_buf[L], send_bytes, irecv_req+L);
        conn_list[R]->irecv(recv_buf[R], send_bytes, irecv_req+R);

        conn_list[L]->wait(isend_req+L);
        conn_list[R]->wait(isend_req+R);
        conn_list[L]->wait(irecv_req+L);
        conn_list[R]->wait(irecv_req+R);

        if(myrank == 0) SUCC("[rank:%d] iter %d.\n", myrank, iter);
    }
    double time_consume = _timer.elapsed();
    SUCC("time %.6lfs\n", time_consume);

}
