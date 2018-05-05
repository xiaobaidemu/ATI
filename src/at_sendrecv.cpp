#include "at_sendrecv.h"
#include <rdma_src/conn_system.h>
#include <tcp_src/tcp_conn_system.h>
bool comm_system::verifyrdma()
{
    char line[1024];
    FILE *fp;
    std::string cmd = "lsmod | grep ib_core";
    const char *sysCommand = cmd.data();
    if ((fp = popen(sysCommand, "r")) == NULL) {
        ERROR("Some thing error when verify the status of network.\n");
        exit(0);
    }
    int linenum = 0, ib_mod_num = 0;
    while (fgets(line, sizeof(line)-1, fp) != NULL){
        linenum ++;
    }
    pclose(fp);
    if (access("/dev/infiniband", 0) == -1){
        IDEBUG("infiniband device is not exist.\n");
        return false;
    }
    else{
        cmd = "ls -h /dev/infiniband |wc -l";
        FILE *fp;
        const char *testib = cmd.data();
        if ((fp = popen(testib, "r")) == NULL) {
            ERROR("Some thing error when verify the status of network.\n");
            exit(0);
        }
        while (fgets(line, sizeof(line)-1, fp) != NULL){
            IDEBUG("ib_mod_num is %d\n", atoi(line));
        }
        ib_mod_num = atoi(line);
        pclose(fp);
    }
    if(linenum > 0 && ib_mod_num > 1)
    {
        NOTICE("linenum %d, ib_mod_num %d\n", linenum, ib_mod_num);
        return true;
    }
    else{
        NOTICE("linenum %d, ib_mod_num %d\n", linenum, ib_mod_num);
        return false;
    }
}

async_conn_system* comm_system::get_conn_system(){
#ifdef IBEXIST
    if(verifyrdma())
        conn_sys_type = RDMA_SYSTEM;
    else
        conn_sys_type = TCP_SYSTEM;
    if(conn_sys_type == RDMA_SYSTEM)
        return new conn_system(my_ip, my_port);
    else{
        return new tcp_conn_system(my_ip, my_port);
    }
#endif
    conn_sys_type = TCP_SYSTEM;
    return new tcp_conn_system(my_ip, my_port);
}

