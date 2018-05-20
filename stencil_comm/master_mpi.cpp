

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PX 3
#define PY 3
#define T 9
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
    printf("myrank : %d, left : %d, right :%d.\n", r, left, right);
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
    double t=-MPI_Wtime(); // take time
    // allocate communication buffers

    for(int iter=0; iter<ITERS; ++iter) {
        // exchange data with neighbors

        if(r == 0){
            MPI_Request reqs_send[T]; MPI_Request reqs_recv[T];
            for(int i=1;i < T;i++){
                MPI_Irecv(recv_buf[i], send_bytes, MPI_CHAR, i, 123, comm, &reqs_recv[i]);
            }
            for(int i=1;i < T;i++){
                MPI_Wait(&reqs_recv[i], NULL);
            }
            for(int i=1;i < T;i++){
                MPI_Isend(dummy_data[i], send_bytes, MPI_CHAR, i, 123, comm, &reqs_send[i]);
            }
            for(int i=1;i < T;i++){
                MPI_Wait(&reqs_send[i], NULL);
            }
        }
        else{
            MPI_Request reqs_send, reqs_recv;
            MPI_Isend(dummy_data[0], send_bytes, MPI_CHAR, 0, 123, comm, &reqs_send);
            MPI_Wait(&reqs_send, NULL);
            MPI_Irecv(recv_buf[0], send_bytes, MPI_CHAR, 0, 123, comm, &reqs_recv);
            MPI_Wait(&reqs_recv, NULL);
        }

        if(r == 0)printf("iters:%d.\n", iter);
        //MPI_Waitall(8, reqs, MPI_STATUS_IGNORE);
    }
    t+=MPI_Wtime();

    // get final heat in the system
    printf("[myrank:%d] time: %f\n",r, t);

    MPI_Finalize();
}

