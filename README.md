# Assignment-4: Enhancing xv6 OS

## Running the operating system

Make sure that the pre-requisites have been installed. Once they have been installed, we can run the operating system as follows:

```bash
# If you want to run the OS in Normal mode
make qemu SCHEDULER=$SCHED_NAME CPUS=$CPU_NUMBERS
```

Alternatively, the code can be debugged in GDB as follows:

```bash
# If you want to run the OS in Normal mode
make qemu-gdb SCHEDULER=$SCHED_NAME CPUS=$CPU_NUMBERS
```

## Implementational aspects

The functionalities implemented in this assignment includes `strace` , scheduling algorithms like `fcfs`, `pbs` and `mlfq`. The details of how the following were implemented is as follows:

### FCFS

The creation time for each of the processes was stored in `allocproc()`function. In the `scheduler()` function, the minimum runnable process was selected and the context of the process was switched with CPU. Since the process is non-preemptive, the `kerneltrap()` and the `usertrap()` functions are changed such that timer interrupts generated do not yield the current running process.

### PBS

In order to implement `PBS`, the `setpriority` system call takes in two arguments, the new priority for the process and the pid of the process. The system call can be implemented by typing the following command:

```bash
setpriority <priority> <pid>
```

Once this is done, the process compares the new static priority with the current dynamic priority. If the new static priority is lesser than the new dynamic priority, then `yield()` is called and the process is preempted. The scheduler runs again. While setting the priority of a process, the value of niceness if reset to 5 and the process is treated as if it were a new process. The process with the least dynamic priority is chosen to be scheduled. Tiebreakers have been implemented appropriately as mentioned in the document. 

### MLFQ

In order to implement `MLFQ`, five types of queues are initialised. Each of these queues can hold `NPROCS`. The PID of a process is stored in these queues. During the cycle of the execution, the process moves between different queues and hence has different time slices for execution. The arrays `time_slice` and `ageing_threshold` holds the time quanta per run and the ageing threshold for each of the queues. To avoid starvation, different ages have been considered for different queues. While scheduling, a non-empty queue is first selected and then a process from this non-empty queue is selected. This process is scheduled. If the process is runnable, context is switched. During ageing, a process moves from a lower priority queue into a higher priority queue. Conversely, during overshot, a process moves from a lower priority queue to a higher priority queue. Overshots have been handled in `usertrap` and `kerneltrap` functions in `trap.c`. 

### STRACE

The `strace` system call has been handled in `strace.c`. The mask is set to the process and is inherited while forking the parent process. The system calls indexed by the set bits of the mask are tracked . Strace can be executed as follows:

```bash
strace <mask> <command>
```

## Procdump

Procdump has been implemented for each process in accordance to the specifications.

## Discussion - Answer to Specification-2 Question

A process relinquishing IO wouldn't be needing CPU time. It will be in the `SLEEP` state waiting for IO to happen. Assume that the IO for this process has been over. Such a process would be placed at the rear-end of the scheduling queue. This gives an opportunity for the CPU bound process to execute while the IO bound process waits for IO. Furthermore, if the number of processes is high, this considerably reduces searching overhead. These processes are usually not `RUNNABLE` and hence won't be scheduled anyways. Hence, this maximises CPU utilisation. 

## Results

A total of 10 processes were considered out of which 7 of them were `IO` bound processes. The results are given as follows:

MLFQ:

```bash
Process 11 finished 
Process 12 finished 
Process 13 finished 
Process 4 finished 
Process 5 finished 
Process 6 finished 
Process 7 finished 
Process 8 finished 
Process 9 finished 
Process 10 finished 
Average rtime 8,  wtime 151
```

PBS:

```bash
$ schedulertest
Process 11 finished 
Process 12 finished 
Process 13 finished 
Process 9 finished 
Process 10 finished 
Process 4 finished 
Process 5 finished 
Process 6 finished 
Process 7 finished 
Process 8 finished 
Average rtime 8,  wtime 148
```

FCFS:

```bash
Process 4 finished 
Process 5 finished 
Process 6 finished 
Process 7 finished 
Process 8 finished 
Process 9 finished 
Process 10 finished 
Process 11 finished 
Process 12 finished 
Process 13 finished 
Average rtime 27,  wtime 123
```

RR:

```bash
Process 12 finished 
Process 11 finished 
Process 13 finished 
Process 4 finished 
Process 5 finished 
Process 6 finished 
Process 7 finished 
Process 8 finished 
Process 9 finished 
Process 10 finished 
Average rtime 8,  wtime 156
```

The high waiting time of MLFQ can be attributed to a high overhead in queue insertion and queue deletion.  We tabulate the average wait time and run time as follows:

[Table-1](https://www.notion.so/3d42b1ccbb3a4e338db120df7444626f)

## Bonus

We consider 2 IO bound process and 8 CPU bound process with sleep(1) for process initiation the given premise. Since ageing is not uniform, the graph hence obtained is as follows:

![Untitled](Assignment-4%20Enhancing%20xv6%20OS%20289e7d8501ee40849ba7df7fd51ab1b5/Untitled.png)

For 10 CPU bound and 2 IO bound processes without sleep during process initiation, we obtain the following:

![Untitled](Assignment-4%20Enhancing%20xv6%20OS%20289e7d8501ee40849ba7df7fd51ab1b5/Untitled%201.png)