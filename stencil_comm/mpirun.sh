#!/bin/bash
src_path="/home/cuixiang/hbx/Sendrecv/build"
((i=0))
while read hostip port;do
    echo "start process on ${hostip}"
    echo "cd ${src_path} && ./test_circle -i ${i} -f $1 $2 $3 -t $4"
    ssh -n $hostip "cd ${src_path} && ./test_circle -i ${i} -f $1 $2 $3 -t $4" &#> out/$i-outf.out 2>&1 &
    ((i++))
done < start_hostfile
#./mpirun.sh ~/hbx/Sendrecv/stencil_comm/hostfile_9 -k 1 1000
