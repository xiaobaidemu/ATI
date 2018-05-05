#!/bin/bash
maxpid=2
src_path="/home/cuixiang/hbx/Sendrecv/build"
((i=0))
while read hostip port;do
    echo "start process on ${hostip}"
    echo "cd ${src_path} && test_tcp -i ${i} -f $1 $2 $3 -t $4"
    #ssh -n $hostip "cd ${src_path} && test_tcp -i ${i} -f $2 $2 $3 -t $4" &# > out/$i-outf.out 2>&1 &
    ((i++))
done < hostfile
