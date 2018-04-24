#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#define BILLION 1000000000
#define INCREMENT 500000000
typedef unsigned int uint;

void increment_clock(uint *sec, uint *nsec)
{
    if ((*nsec + INCREMENT) >= BILLION)
    {
        *sec = *sec + 1;
        *nsec = (*nsec + INCREMENT) - BILLION;
    }
    else
    {
        *nsec += INCREMENT;
    }
}

int main (int argc, char * argv[])
{
    time_t t;
    uint sec = 0;
    uint nsec = 0;
    srand((unsigned) time(&t));
    int maxprocs = 5;
    int currentprocs = 0;
    int pid;
    char * argarray[] = {"./user", NULL};
    int status;
    pid_t wpid;

    while ((sec < 10) && (currentprocs < maxprocs))
    {
        printf("Clock is %u:%u\n", sec, nsec);
        increment_clock(&sec, &nsec);
        printf("The clock is now %u:%u\n", sec, nsec);
        while ((wpid = wait(&status)) >= maxprocs);
        pid = fork();
        currentprocs++;
        if (pid == 0)
        {
            if(execvp(argarray[0], argarray) < 0) //execute user
            {
                printf("Execution failed!\n");
                return 1;
            }
        }
    }
    while ((wpid = wait(&status)) > 0);
    return 0;
}