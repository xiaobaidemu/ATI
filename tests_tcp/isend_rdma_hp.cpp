#include <rdma_src/conn_system.h>
#include <iostream>
#include <sstream>
#include <fstream>
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
        DEBUG("rank:%d ip_addr:%s port:%d\n", i, nodelist[i].ip_addr, nodelist[i].listen_port);
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
    char* dummy_data = (char*)malloc(send_bytes);
    for (size_t i = 0; i < send_bytes; ++i) {
        dummy_data[i] = (char)(unsigned char)i;
    }
    //show basic info about myself
    int peerrank = (allsize - myrank)/allsize;
    SUCC("%s:%d ready to init with %s:%d.\n", nodelist[myrank].ip_addr, nodelist[myrank].listen_port,
         nodelist[peerrank].ip_addr, nodelist[peerrank].listen_port);
    conn_system sys(nodelist[myrank].ip_addr, nodelist[myrank].listen_port);
    rdma_conn_p2p *rdma_conn_object = (rdma_conn_p2p*)sys.init(nodelist[peerrank].ip_addr,
                                                               nodelist[peerrank].listen_port);
    ASSERT(rdma_conn_object);
    SUCC("%s:%d init finished.\n", nodelist[myrank].ip_addr, nodelist[myrank].listen_port);

    char *recv_buf = (char*)malloc(send_bytes);

    //const void *buf, size_t count, non_block_handle *req, oneside_info *peer_info
    non_block_handle hp_isend_init_req,  hp_irecv_req;
    oneside_info send_info, recv_info;
    if(myrank == 0){
        rdma_conn_object->hp_isend_init(&hp_isend_init_req, &send_info);
        rdma_conn_object->wait(&hp_isend_init_req);
        SUCC("[Finished HP_SEND oneside exchange info] recv_buffer %llx, rkey %d\n",
             (long long)send_info.recv_buffer, (int)send_info.rkey);
    }
    else{
        rdma_conn_object->hp_irecv_init(recv_buf, send_bytes, &hp_irecv_req, iters, &recv_info);
        rdma_conn_object->wait(&hp_irecv_req);
        SUCC("[Finished RECV oneside exchange info] recv_buffer %llx, rkey %d\n",
             (long long)recv_info.recv_buffer, (int)recv_info.rkey);
    }
    non_block_handle hp_isend_req;
    timer _timer;
    for(int iter = 0; iter < iters; iter++){
        if(myrank == 0){
            //ITR_SPECIAL("oneside_isend begin %d time.\n", iter);
            rdma_conn_object->hp_isend(dummy_data, send_bytes, &hp_isend_req, &send_info);
            rdma_conn_object->wait(&hp_isend_req);
            //SUCC("[hp_isend] iter %d.\n", iter);
            //ITR_SPECIAL("oneside_isend end %d time.\n", iter);
        }
    }
    if(myrank == 1){
        rdma_conn_object->hp_recv_wait(&hp_irecv_req, iters);
        SUCC("[all recv finish.\n]......");
    }
    double time_consume = _timer.elapsed();
    size_t total_size = send_bytes*iters;
    double speed = (double)total_size/1024/1024/time_consume;
    if(myrank == 0)
    {
        SUCC("[HP_ISEND] time %.6lfs, total_size %lld bytes, speed %.2lf MB/sec\n", time_consume,
             (long long)total_size, speed);
    }
    else
    {
        SUCC("[HP_IRECV] time %.6lfs, total_size %lld bytes, speed %.2lf MB/sec\n", time_consume,
             (long long)total_size, speed);
    }
}