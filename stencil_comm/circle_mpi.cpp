

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PX 3
#define PY 3
#define T 2
#define L 0
#define R 1
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
    int left  = (r-1+p)%p;
    int right = (r+1)%p;
    int maxrank = p-1;
    // determine my coordinates (x,y) -- r=x*a+y in the 2d processor array

    double t=-MPI_Wtime(); // take time
    // allocate communication buffers
    char* dummy_data[T], *recv_buf[T];
    for(int i = 0;i < T;i++){
        dummy_data[i] = (char*)malloc(send_bytes);
        recv_buf[i] = (char*)malloc(send_bytes);
    }
    for(int j = 0;j < T;j++){
        for (size_t i = 0; i < send_bytes; ++i) {
            dummy_data[j][i] = (char)(unsigned char)i;
        }
    }

    for(int iter=0; iter<ITERS; ++iter) {
        // exchange data with neighbors
        MPI_Request reqs_send[T]; MPI_Request reqs_recv[T];

        MPI_Isend(dummy_data[L], send_bytes, MPI_CHAR, left, 123, comm, &reqs_send[L]);
        MPI_Isend(dummy_data[R], send_bytes, MPI_CHAR, right, 123, comm, &reqs_send[R]);

        MPI_Irecv(recv_buf[L], send_bytes, MPI_CHAR, left, 123, comm, &reqs_recv[L]);
        MPI_Irecv(recv_buf[R], send_bytes, MPI_CHAR, right, 123, comm, &reqs_recv[R]);

        MPI_Wait(&reqs_send[L], NULL);
        MPI_Wait(&reqs_send[R], NULL);
        MPI_Wait(&reqs_recv[L], NULL);
        MPI_Wait(&reqs_recv[R], NULL);

        if(r == 0)printf("iters:%d.\n", iter);
        //MPI_Waitall(8, reqs, MPI_STATUS_IGNORE);
    }
    t+=MPI_Wtime();

    // get final heat in the system
    printf("time: %f\n", t);

    MPI_Finalize();
}
