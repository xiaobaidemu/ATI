#########################################################################
# File Name: kill_all.sh
# Author: He Boxin
# mail: heboxin@pku.edu.cn
# Created Time: Tue 08 May 2018 11:57:54 AM CST
#########################################################################
#!/bin/bash
src_path="/home/cuixiang/hbx/Sendrecv/stencil_comm"
while read hostip;do
    echo "kill process on ${hostip}"
    ssh -n $hostip "cd ${src_path} && ./kill.sh"
    process_num=$(ssh -n $hostip  " ps -Af | grep 'test_stencil' | grep -v grep | grep -v qemu | wc -l")
    echo "rest procress num $process_num"
done < start_hostfile
