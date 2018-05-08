#########################################################################
# File Name: kill.sh
# Author: He Boxin
# mail: heboxin@pku.edu.cn
# Created Time: Tue 08 May 2018 11:55:13 AM CST
#########################################################################
#!/bin/bash
pids=$(ps -Af | grep test_stencil | grep -v grep | awk '{print $2}')
if [[ $pids == "" ]] ; then
     ps -Af | grep test_stencil | grep -v grep | awk '{print $2}'
fi

for pid in $pids;do
    kill -9 $pid
done

exit 0
