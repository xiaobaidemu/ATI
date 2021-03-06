
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
    char* dummy_data_n = (char*)malloc(send_bytes);
    char* dummy_data_s = (char*)malloc(send_bytes);
    char* dummy_data_e = (char*)malloc(send_bytes);
    char* dummy_data_w = (char*)malloc(send_bytes);
    char *recv_buf_n = (char*)malloc(send_bytes);
    char *recv_buf_s = (char*)malloc(send_bytes);
    char *recv_buf_e = (char*)malloc(send_bytes);
    char *recv_buf_w = (char*)malloc(send_bytes);

    for (size_t i = 0; i < send_bytes; ++i) {
        dummy_data_n[i] = (char)(unsigned char)i;
        dummy_data_s[i] = (char)(unsigned char)i;
        dummy_data_e[i] = (char)(unsigned char)i;
        dummy_data_w[i] = (char)(unsigned char)i;
    }

    std::vector<rdma_conn_p2p*> conn_list(PX*PY);
    conn_system sys(nodelist[myrank].ip_addr, nodelist[myrank].listen_port);
    int rx = myrank % PX;
    int ry = myrank / PY;

    // determine my four neighbors
    int north = (ry-1+PY)%PY*PX+rx;
    int south = (ry+1)%PY*PX+rx;
    int west= ry*PX+(rx-1+PX)%PX;
    int east = ry*PX+(rx+1)%PX;
    //SUCC("myrank:%d (%d,%d) N:%d, S:%d, W:%d, E:%d\n", myrank, rx, ry, north, south, west, east);

    for(int i = 0;i < PX*PY;i++){
        if(myrank != i)
            conn_list[i] = (rdma_conn_p2p*)sys.init(nodelist[i].ip_addr, nodelist[i].listen_port);
        else
            conn_list[i] = nullptr;
    }


    SUCC("%s:%d init finished.\n", nodelist[myrank].ip_addr, nodelist[myrank].listen_port);


    non_block_handle isend_req[4];
    non_block_handle irecv_req[4];
    timer _timer;

    for(int iter = 0; iter < iters; iter++){
        conn_list[north]->isend(dummy_data_n, send_bytes, isend_req+N);
        conn_list[south]->isend(dummy_data_s, send_bytes, isend_req+S);
        conn_list[west]->isend(dummy_data_w, send_bytes, isend_req+W);
        conn_list[east]->isend(dummy_data_e, send_bytes, isend_req+E);

        conn_list[north]->irecv(recv_buf_n, send_bytes, irecv_req+N);
        conn_list[south]->irecv(recv_buf_s, send_bytes, irecv_req+S);
        conn_list[west]->irecv(recv_buf_w, send_bytes, irecv_req+W);
        conn_list[east]->irecv(recv_buf_e, send_bytes, irecv_req+E);

        conn_list[north]->wait(isend_req+N);
        conn_list[south]->wait(isend_req+S);
        conn_list[west]->wait(isend_req+W);
        conn_list[east]->wait(isend_req+E);

        conn_list[north]->wait(irecv_req+N);
        conn_list[south]->wait(irecv_req+S);
        conn_list[west]->wait(irecv_req+W);
        conn_list[east]->wait(irecv_req+E);
        if(myrank == 0) SUCC("[rank:%d] iter %d.\n", myrank, iter);
    }
    double time_consume = _timer.elapsed();
    SUCC("time %.6lfs\n", time_consume);

}
