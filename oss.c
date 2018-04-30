/*
 * Joshua Bearden
 * CS 4760 Project 6
 * 4-29-18
 *
 * This project is incomplete. It is supposed to create processes that will request memory addresses
 * and the OSS will determine if those addresses are valid and simulate paging, while handling a queue of
 * processes blocked on I/O in case of a page fault.
 *
 * This program contains code for the message queues, the simulated clock, the arrays to hold all the
 * pages, functions to kill the process and all children on interrupt, functions to determine when to
 * launch a new process and basic code to fork a process.
 *
 */
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <string.h>
#include "clock.c"
#include <stdbool.h>


#define MSGKEY 110992
#define MSGKEYSTR "110992"
#define BILLION 1000000000
#define MILLISEC 1000000
#define PR_LIMIT 18
#define MAX_SECONDS 2

// Declare some global variables so that shared memory can be cleaned from the interrupt handler
//int ClockID;
//struct clock *Clock;
int MsgID;
FILE *fp;

struct mesg_buf {
    long mtype;
    char mtext[100];
} message;

// A function that catches SIGINT and SIGALRM
// It prints an alert to the screen then sends a signal to all the child processes to terminate,
// frees all the shared memory and closes the file.
static void interrupt(int signo, siginfo_t *info, void *context)
{
    int errsave;

    errsave = errno;
    write(STDOUT_FILENO, TIMER_MSG, sizeof(TIMER_MSG) - 1);
    errno = errsave;
    signal(SIGUSR1, SIG_IGN);
    kill(-1*getpid(), SIGUSR1);
    msgctl(MsgID, IPC_RMID, NULL);
    fclose(fp);
    exit(1);
}

// A function from the setperiodic code, it sets up the interrupt handler
static int setinterrupt()
{
    struct sigaction act;

    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = interrupt;
    if (((sigemptyset(&act.sa_mask) == -1) || (sigaction(SIGALRM, &act, NULL) == -1)) || sigaction(SIGINT, &act, NULL) == -1)
    {
        return 1;
    }
    return 0;
}

// A function that determines if some target time has passed.
int hasTimePassed(struct clock current, struct clock dest)
{
    if (dest.sec > current.sec)  // if destination.sec is greater than current.sec, it's definitely later
    {
        return 1;
    }
    else if (dest.sec == current.sec) // otherwise if the seconds are equal, check the nanoseconds
    {
        if (dest.nsec >= current.nsec) // if destination ns is greater, it's later
        {
            return 1;
        }
    }
    return 0;  // otherwise, time has not passed, return false
}

// A function to get a random time between 0 and 500 milliseconds from now
struct clock getNextProcTime(struct clock c)
{
    // get a random number between 0 and 500 milliseconds
    nsecs = rand() % (500 * MILLISEC);

    // make a new clock object initialized to the current time
    struct clock newClock;
    newClock.nsec = c.nsec;
    newClock.sec = c.sec;

    // if adding the randomly generated amount of time causes us to move to the next second, increment sec
    if ((newClock.nsec + nsecs) >= BILLION)
    {
        newClock.sec = newClock.sec + 1;
        newClock.nsec = nsecs - BILLION;
    }
    else // otherwise, just add to nsec
    {
        newClock.nsec = newClock.nsec + nsecs;
    }
    return newClock; // return the new clock object
}


int main(int argc, char * argv[]) {

    // initialize all the variables
    int i, pid, c, status, memory;
    int maxprocs = 0;
    int pr_count = 0;
    int totalprocs = 0;
    char *filename;
    pid_t wait = 0;
    bool timeElapsed = false;
    char messageString[100];
    int proctime;
    int procendsec;
    int procendnsec;
    char *temp;
    int bit_array[256];
    int dirty_bit[256];
    int ref_bit[256];
    struct clock Clock;

    struct clock endTime;
    endTime.nsec = 0;
    endTime.sec = MAX_SECONDS;

    // seed the random number generator with the process pid
    srand(getpid());

    // initialize all the arrays
    for (i = 0; i < 256; i++)
    {
        bit_array[i] = 0;
        dirty_bit[i] = 0;
        ref_bit[i] = 0;
    }

    // Process command line arguments
    if(argc == 1) //if no arguments passed
    {
        printf("ERROR: command line options required, please run ./oss -h for usage instructions.\n");
        return 1;
    }

    while ((c = getopt(argc, argv, "hs:l:")) != -1)
    {
        switch(c)
        {
            case 'h': // -h for help
                printf("Usage: ./oss [-s x] [-t z] -l filename\n");
                printf("-s x: x is the maximum number of concurrent processes, this is a required argument.\n");
                printf("-l filename: filename is the name you would like the log file to have. This is a required argument\n");
                return 0;
            case 's': // -s for max number of processes
                if(isdigit(*optarg))
                {
                    maxprocs = atoi(optarg);
                    if(maxprocs > PR_LIMIT)
                    {
                        printf("Error: Too many processes. No more than 18 allowed.\n");
                        return(1);
                    }
                    printf("Max %d processes\n", maxprocs);
                }
                else
                {
                    printf("Error, -s must be followed by an integer!\n");
                    return 1;
                }
                break;
            case 'l': // -l for filename (required!)
                filename = optarg;
                printf("Log file name is: %s\n", filename);
                break;
            default: // anything else, fail
                printf("Expected format: [-s x] -l filename \n");
                printf("-s for max number of processes, -l for log file name.\n");
                return 1;
        }
    }

    if(!filename) //ensure filename was passed
    {
        printf("Error! Must specify a filename with the -l flag, please run ./oss -h for more info.\n");
        return(1);
    }

    if(maxprocs == 0) // ensure max processes was passed
    {
        printf("Error! Must specify the number of processes with the -s flag, please run ./oss -h for more info.\n");
        return(1);
    }


    // Set the timer-kill
    if (setinterrupt() == -1)
    {
        perror("Failed to set up handler");
        return 1;
    }


    // initialize the clock
    Clock.sec = 0;
    Clock.nsec = 0;

    // Create the message queue
    MsgID = msgget(MSGKEY, 0666 | IPC_CREAT);

    // open file
    fp = fopen(filename, "w");

    // create the pid_array now that we know the max processes
    int pid_array[maxprocs+1];

    // initialize simpid variable and the string to allow us to pass simpid as an argument to a child proc
    int simpid;
    char stringSimPid[5];

    // get the next time to launch a process
    struct clock nextproc;
    nextproc = getNextProcTime(Clock);

    // while we haven't hit the end time for the program
    while(!hasTimePassed(Clock, endTime))
    {
        // if its time to fork a new process
        if (hasTimePassed(Clock, nextproc))
        {
            // if we still have room to fork a new process
            if (pr_count < maxprocs)
            {
                // increment the process count
                pr_count = pr_count + 1;

                // find the next empty spot in the pid_array, save that index as simpid
                for (i = 1; i < maxprocs + 1; i++)
                {
                    if (pid_array[i] == 0)
                    {
                        simpid = i;
                    }
                }

                // convert simpid to a string
                sprintf(stringSimPid, "%i", simpid);

                // fork the process
                pid = fork();
                if (pid == 0) // if this is the child, exec user
                {
                    char *argarray[] = {"./user", MSGKEYSTR, stringSimPid, NULL}; // pass message queue key and simpid
                    if (execvp(argarray[0], argarray) < 0)
                    {
                        printf("Execution failed!\n"); // if we ever get here, exec failed
                        return 1;
                    }
                } else if (pid < 0) // if pid is negative, fork failed
                {
                    printf("Fork failed!\n");
                    return 1;
                }
                // only the parent executes this - store the child's pid in the pid_array at index simpid
                pid_array[simpid] = pid;

                // log process creation
                fprintf(fp, "Master: Creating child process %d at my time %d.%d\n", pid, Clock.sec, Clock.nsec);
            }
            // get the next time to launch a process
            nextproc = getNextProcTime(Clock);
        }
        // check if all procs are blocked


        // receive a message
        msgrcv(MsgID, &message, sizeof(message), 0, 0);

        // check if its initalization, termination, or request

        // if initalization, initialize page table

        // if termination, clear memory

        // if request, check if valid, handle

    }

    // we're done, detach and free shared memory and close the file
    // then send a kill signal to the children and wait for them to exit
    msgctl(MsgID, IPC_RMID, NULL);
    fclose(fp);
    signal(SIGUSR1, SIG_IGN);
    kill(-1*getpid(), SIGUSR1);
    while(pr_count > 0)
    {
        wait = waitpid(-1, &status, WNOHANG);
        if(wait != 0)
        {
            pr_count--;
        }
    }
    printf("Exiting normally\n");
    return 0;
}
