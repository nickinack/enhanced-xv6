#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main(int argc, char **argv) {
    if (argc != 3)
    {
        printf("setpriority: invalid arguments \n");
    }
    int pid = fork();
    if (pid < 0)
    {
        printf("setpriority: fork failed \n");
        return -1;
    }
    else if (pid == 0)  //child
    {
        printf("setpriority: args %s \n%s \n", argv[1], argv[2]);
        int priority = atoi(argv[1]);
        int pid = atoi(argv[2]);
        setpriority(priority, pid);
        exit(1);
    }
    else    //parent
    {
        wait(0);
        printf("spriority: child process with pid: %d done running \n", pid);
    }
    exit(0);
}