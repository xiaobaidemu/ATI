
#include <rdma_src/conn_system.h>
#include <iostream>
#include <sstream>
#include <fstream>
#define PX 3
#define PY 3
#define DIRECTION 4
#define N 0
#define S 1
#define W 2
#define E 3
#define T 9
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
    char* dummy_data[DIRECTION], *recv_buf[DIRECTION];
    for(int i = 0;i < DIRECTION;i++){
        dummy_data[i] = (char*)malloc(send_bytes);
        recv_buf[i] = (char*)malloc(send_bytes);
    }
    for(int j = 0;j < DIRECTION;j++)
        for (size_t i = 0; i < send_bytes; ++i) {
            dummy_data[j][i] = (char)(unsigned char)i;
        }

    std::vector<rdma_conn_p2p*> conn_list(DIRECTION);
    conn_system sys(nodelist[myrank].ip_addr, nodelist[myrank].listen_port);
    int rx = myrank % PX;
    int ry = myrank / PY;

    // determine my four neighbors
    int north = (ry-1+PY)%PY*PX+rx;
    int south = (ry+1)%PY*PX+rx;
    int west= ry*PX+(rx-1+PX)%PX;
    int east = ry*PX+(rx+1)%PX;
    //SUCC("myrank:%d (%d,%d) N:%d, S:%d, W:%d, E:%d\n", myrank, rx, ry, north, south, west, east);

    //1 step
    if(myrank%PX==0){
        conn_list[E] = (rdma_conn_p2p*)sys.init(nodelist[east].ip_addr, nodelist[east].listen_port);
    }
    else if(myrank%PX==1){
        conn_list[W] = (rdma_conn_p2p*)sys.init(nodelist[west].ip_addr, nodelist[west].listen_port);
    }
    //2 step
    if(myrank < PX){
        conn_list[S] = (rdma_conn_p2p*)sys.init(nodelist[south].ip_addr, nodelist[south].listen_port);
    }
    else if(myrank >= PX && myrank < 2*PX){
        conn_list[N] = (rdma_conn_p2p*)sys.init(nodelist[north].ip_addr, nodelist[north].listen_port);
    }
    //3 step
    if(myrank%PX==1){
        conn_list[E] = (rdma_conn_p2p*)sys.init(nodelist[east].ip_addr, nodelist[east].listen_port);
    }
    else if(myrank%PX==2){
        conn_list[W] = (rdma_conn_p2p*)sys.init(nodelist[west].ip_addr, nodelist[west].listen_port);
    }
    //4 step
    if(myrank >= PX && myrank < 2*PX){
        conn_list[S] = (rdma_conn_p2p*)sys.init(nodelist[south].ip_addr, nodelist[south].listen_port);
    }
    else if(myrank >= 2*PX){
        conn_list[N] = (rdma_conn_p2p*)sys.init(nodelist[north].ip_addr, nodelist[north].listen_port);
    }
    //5 step
    if(myrank%3== 0){
        conn_list[W] = (rdma_conn_p2p*)sys.init(nodelist[west].ip_addr, nodelist[west].listen_port);
    }
    else if(myrank%3==2){
        conn_list[E] = (rdma_conn_p2p*)sys.init(nodelist[east].ip_addr, nodelist[east].listen_port);
    }
    //6 step
    if(myrank<PX){
        conn_list[N] = (rdma_conn_p2p*)sys.init(nodelist[north].ip_addr, nodelist[north].listen_port);
    }
    else if(myrank>=2*PX){
        conn_list[S] = (rdma_conn_p2p*)sys.init(nodelist[south].ip_addr, nodelist[south].listen_port);
    }

    for(int i=0;i < DIRECTION;i++)
        ASSERT(conn_list[i]);
    SUCC("rank %d connected successed.\n", myrank);


    SUCC("%s:%d init finished.\n", nodelist[myrank].ip_addr, nodelist[myrank].listen_port);


    non_block_handle isend_req[DIRECTION];
    non_block_handle irecv_req[DIRECTION];
    timer _timer;

    for(int iter = 0; iter < iters; iter++){
        for(int i = 0;i < DIRECTION;i++){
            conn_list[i]->isend(dummy_data[i], send_bytes, isend_req+i);
        }
        for(int i = 0;i < DIRECTION;i++){
            conn_list[i]->irecv(recv_buf[i], send_bytes, irecv_req+i);
        }
        for(int i = 0;i < DIRECTION;i++){
            conn_list[i]->wait(isend_req+i);
        }
        for(int i = 0;i < DIRECTION;i++){
             conn_list[i]->wait(irecv_req+i);
        }
        if(myrank == 0) SUCC("[rank:%d] iter %d.\n", myrank, iter);
    }
    double time_consume = _timer.elapsed();
    SUCC("time %.6lfs\n", time_consume);

}
