#!/bin/bash
maxpid=2
src_path="/home/cuixiang/hbx/Sendrecv/build"
((i=0))
while read hostip port;do
    echo "start process on ${hostip}"
    echo "cd ${src_path} && ./tests_hp_isend -i ${i} -f $1 $2 $3 -t $4"
    ssh -n $hostip "cd ${src_path} && ./tests_hp_isend -i ${i} -f $1 $2 $3 -t $4" &# > out/$i-outf.out 2>&1 &
    ((i++))
done < start_hostfile
#./mpirun.sh ~/hbx/Sendrecv/tests_tcp/hostfile -k 1 1000
