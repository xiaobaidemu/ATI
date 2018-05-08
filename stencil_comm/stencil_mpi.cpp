#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PX 3
#define PY 3
#define ITERS 1000
int main(int argc, char **argv) {
    size_t size = (size_t)atol(argv[1]);
    char   unit = argv[2][0];
    size_t send_bytes;
    switch(unit){
        case 'b':
            send_bytes = size;
            break;
        case 'k':
            send_bytes = size * 1024;
            break;
        case 'm':
            send_bytes = size * 1024 *1024;
            break;
    }
    printf("send_bytes: %lld.\n", (long long)send_bytes);
    MPI_Init(&argc, &argv);
    int r,p;
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Comm_rank(comm, &r);
    MPI_Comm_size(comm, &p);

    // determine my coordinates (x,y) -- r=x*a+y in the 2d processor array
    int rx = myrank % PX;
    int ry = myrank / PY;

    // determine my four neighbors
    int north = (ry-1+PY)%PY*PX+rx;
    int south = (ry+1)%PY*PX+rx;
    int west = ry*PX+(rx-1+PX)%PX;
    int east = ry*PX+(rx+1)%PX;


    double t=-MPI_Wtime(); // take time
    // allocate communication buffers
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


    for(int iter=0; iter<ITERS; ++iter) {
        // exchange data with neighbors
        MPI_Request reqs[8];
        MPI_Isend(dummy_data_n, send_bytes, MPI_CHAR, north, 9, comm, &reqs[0]);
        MPI_Isend(dummy_data_s, send_bytes, MPI_CHAR, south, 9, comm, &reqs[1]);
        MPI_Isend(dummy_data_w, send_bytes, MPI_CHAR, west, 9, comm, &reqs[2]);
        MPI_Isend(dummy_data_e, send_bytes, MPI_CHAR, east, 9, comm, &reqs[3]);
        MPI_Irecv(recv_buf_n, send_bytes, MPI_CHAR, north, 9, comm, &reqs[4]);
        MPI_Irecv(recv_buf_s, send_bytes, MPI_CHAR, south, 9, comm, &reqs[5]);
        MPI_Irecv(recv_buf_w, send_bytes, MPI_CHAR, west, 9, comm, &reqs[6]);
        MPI_Irecv(recv_buf_e, send_bytes, MPI_CHAR, east, 9, comm, &reqs[7]);
        MPI_Wait(&reqs[0], nullptr);
        MPI_Wait(&reqs[1], nullptr);
        MPI_Wait(&reqs[2], nullptr);
        MPI_Wait(&reqs[3], nullptr);
        MPI_Wait(&reqs[4], nullptr);
        MPI_Wait(&reqs[5], nullptr);
        MPI_Wait(&reqs[6], nullptr);
        MPI_Wait(&reqs[7], nullptr);
        if(r == 0)printf("iters:%d.\n", iter);
        //MPI_Waitall(8, reqs, MPI_STATUS_IGNORE);
    }
    t+=MPI_Wtime();

    // get final heat in the system
    printf("time: %f\n", t);

    MPI_Finalize();
}
