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
    char* dummy_data_1 = (char*)malloc(send_bytes);
    char* dummy_data_2 = (char*)malloc(send_bytes);
    char *recv_buf_1 = (char*)malloc(send_bytes);
    char *recv_buf_2 = (char*)malloc(send_bytes);

    for (size_t i = 0; i < send_bytes; ++i) {
        dummy_data_1[i] = (char)(unsigned char)i;
        dummy_data_2[i] = (char)(unsigned char)i;
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

    timer _timer;
    non_block_handle isend_req_1, isend_req_2,irecv_req_1, irecv_req_2;
    for(int iter = 0; iter < iters; iter++){
        rdma_conn_object->isend(dummy_data_1, send_bytes, &isend_req_1);
        rdma_conn_object->isend(dummy_data_2, send_bytes, &isend_req_2);
        rdma_conn_object->irecv(recv_buf_1, send_bytes, &irecv_req_1);
        rdma_conn_object->irecv(recv_buf_2, send_bytes, &irecv_req_2);
        rdma_conn_object->wait(&isend_req_1);
        rdma_conn_object->wait(&isend_req_2);
        rdma_conn_object->wait(&irecv_req_1);
        rdma_conn_object->wait(&irecv_req_2);
        //ASSERT(memcmp(dummy_data, recv_buf, DATA_LEN) == 0);
    }
    double time_consume = _timer.elapsed();
    size_t total_size = send_bytes*4*iters;
    double speed = (double)total_size/1024/1024/time_consume;

    SUCC("time %.6lfs, total_size %lld bytes, speed %.2lf MB/sec\n", time_consume, (long long)total_size, speed);

}