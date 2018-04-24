#include <stdlib.h>
#include <stdio.h>
#define BILLION 1000000000
#define INCREMENT 500000
typedef unsigned int uint;

void increment_clock(uint *sec, uint *nsec)
{
    if ((*nsec + INCREMENT) > BILLION)
    {
        *sec++;
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

    while ((sec < 2) && (currentprocs < maxprocs))
    {
        print("Clock is %u:%u\n", sec, nsec);
        increment_clock(&sec, &nsec);
        print("The clock is now %u:%u\n", sec, nsec);
//        while (currentprocs >= maxprocs);
        pid = fork();
        currentprocs++;
        if (pid == 0)
        {
            if(execvp("./user", {"./user", NULL}) < 0) //execute user
            {
                printf("Execution failed!\n");
                return 1;
            }
        }
    }
    return 0;
}