#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int 
main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("strace: invalid arguments \n");
    }
    int pid = fork();
    if (pid < 0)
    {
        printf("strace: fork failed \n");
        return -1;
    }
    else if (pid == 0)  //child
    {
        printf("strace: args %s \n%s \n", argv[1], argv[2]);
        int mask = atoi(argv[1]);
        strace(mask);
        exec(argv[2], argv + 2);
        printf("strace: exec failed \n");
        exit(1);
    }
    else    //parent
    {
        wait(0);
        printf("strace: child process with pid: %d done running \n", pid);
    }
    exit(0);
}