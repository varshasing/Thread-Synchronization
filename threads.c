#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include "ec440threads.h"

/* libc indexes for registers in jmp_buf */
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

/* different thread status:
    EXIT: 0
    READY: 1
    RUNNING: 2
    BLOCKED: 3
    EMPTY: 4                    // TCB is empty, for array initalization
*/
#define NUM_THREADS 128         // maximum number of threads alive at the same time
#define STACK_BYTE 32767        // number of bytes to allocate in a threads stack
#define SCHEDULE_TIME_MS 50000  // 50ms quantum, accepted by ualarm in microseconds

TCB TCB_array[NUM_THREADS];     // array of TCBs, used for each thread
pthread_t TID = 0;              // thread ID for currently running thread, needed for scheduling and pthread_self
bool first_call = false;        // flag to indicate if pthread_create has been called before -> switching between single and multithreaded system

struct sigaction handler;       // signal handler for SIGALRM
struct my_sem_t sem_array[NUM_THREADS];  // array of semaphores, used for each semaphore (each process can have a semaphore, max of NUM_THREADS processes)
/*
* struct sigaction {
*     void (*sa_handler)(int);
*     void (*sa_sigaction)(int, siginfo_t *, void *);
*     sigset_t sa_mask;
*     int sa_flags;
*     void (*sa_restorer)(void);
* }
*/
/* helper for initalizing thread subsystem when calling pthread_create for the first time */
void round_robin_schedule()
{
    if(TCB_array[TID].status == 2)      // if it is the current thread is running
    {
        TCB_array[TID].status = 1;      // set status to ready
    }
    pthread_t cur_tid = TID;            // get the current thread ID
    while(1)
    {
        int counter = 1;
        if(cur_tid == NUM_THREADS - 1)
        {
            cur_tid = 0;                // wrap around to the first thread
        }
        else{
            cur_tid++;
        }

        if(TCB_array[cur_tid].status == 1)
        {
            break;                      // found the next ready thread
        }
        counter++;
        if(counter > (NUM_THREADS+6)) // if no ready threads found, exit. Give it some slack just in case...
        {
            perror("No ready threads found");
            exit(EXIT_FAILURE);
        }
    }

    int jmp = 0;                                    // setjmp returns nonzero if returning from longjmp

    if(TCB_array[TID].status != 0)                  // if not ready to exit, then the environment needs to be saved
    {
        jmp = setjmp(TCB_array[TID].registers);     // save the current context for the next thread
    }

    if(!jmp)
    {
        TID = cur_tid;                              // set the current thread to the next ready thread
        TCB_array[TID].status = 2;                  // set status to running
        longjmp(TCB_array[TID].registers, 1);       // jump to the next thread
    }
}

/* to be used when switching the unithreaded program to a multithreaded program */
void first_time()
{
    useconds_t time = SCHEDULE_TIME_MS;     // set time to 50ms
    ualarm(time, time);                     // set up alarm for scheduler
    sigemptyset(&handler.sa_mask);          // ensure that the signal mask is empty
    handler.sa_handler = &round_robin_schedule; // handler is the scheduler
    handler.sa_flags = SA_NODEFER;          // alarms are automatically blocked
    sigaction(SIGALRM, &handler, NULL);     // set up signal handler for SIGALRM, assignment states sigaction
}
/* creates a new thread within a process */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
    lock();                             // lock the current tshread, want this to be atomic

    //attr = NULL;                        // Proj states attr always set to NULL
    int thread_cur = 0;                 // running the calling thread, needs to be started as if in context first time making it multithreaded

    if(!first_call)                     // switching to multithreaded system
    {
        /* initalize scheduler */
        int i;
        for(i = 0; i < NUM_THREADS; i++)
        {
            TCB_array[i].status = 4;    // set all TCBs to empty
            TCB_array[i].tid = i;       // unique id is index in array
            TCB_array[i].exit_code = NULL;  // set exit code to NULL
            TCB_array[i].join_id = -1;  // set join ID to -1
        }

        /* SIGALRM for scheduler timing since multiple threads */
        first_time();
        first_call = true;
        TCB_array[0].status = 2;        // main thread is running
        thread_cur = setjmp(TCB_array[0].registers);    // save current context for thread 0
        /* if returning directly, jump is 0. Else, nonzero when returning from longjmp */
    }
    if(thread_cur == 0)                 // if zero, means that the current thread is running
    {
        pthread_t iter = 1;
        while(TCB_array[iter].status != 4 && iter < NUM_THREADS)    // find the first empty TCB. This works because irst empty TCB is behind the current thread
        {
            iter++;
        }
        if(iter == NUM_THREADS)         // if no empty TCBs, return error. Should never happen.
        {
            perror("No empty TCBs available");
            exit(EXIT_FAILURE);         // assuming that this is not possible, but if there are more than 128 threads it will need to be termianted
        }
        *thread = iter;                 // set thread ID to the first empty TCB

        setjmp(TCB_array[iter].registers);  // save current context for the new thread

        TCB_array[iter].registers[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long)start_thunk);      // set program counter to start_thunk
        TCB_array[iter].registers[0].__jmpbuf[JB_R13] = (unsigned long)arg;                         // set argument to R13
        TCB_array[iter].registers[0].__jmpbuf[JB_R12] = (unsigned long)start_routine;               // set start_routine to R12

        TCB_array[iter].stack = malloc(STACK_BYTE);             // allocate stack for the new thread
        void* stackB = TCB_array[iter].stack + STACK_BYTE;      // set stack pointer to the end of the stack

        void* stackPtr = stackB - sizeof(void*);                // set stack pointer to the end of the stack
        void(*temp)(void*) = (void*) &pthread_exit_wrapper;     // set temp to pthread_exit
        stackPtr = memcpy(stackPtr, &temp, sizeof(temp));       // copy pthread_exit to the stack

        TCB_array[iter].registers[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long)stackPtr);    // set stack pointer to the stack pointer

        TCB_array[iter].status = 1;     // set status to ready
        TCB_array[iter].tid = iter;     // set thread ID to the index in the array

        round_robin_schedule();         // schedule the new thread
    }
    else
    {
        thread_cur = 0;
    }
    unlock();                           // unlock the current thread
    return 0;
}

/* terminates the calling thread */
/* WIP: retain its return value since other threads may want to get this return value by calling pthread_join later
once a thread's exit calue is collected via CALL TO pthread_join, free all resourcecs related to the tread */
void pthread_exit(void *value_ptr)
{
    lock();

    if(TCB_array[TID].tid == TID)
    {
        TCB_array[TID].status = 0;                  // set status to exit
        TCB_array[TID].exit_code = value_ptr;       // collect exit code of the thread
        if(TCB_array[TID].join_id != -1)            // if the thread has been joined, wake up the waiting thread
        {
            TCB_array[TCB_array[TID].join_id].status = 1;  // set status to ready
            TCB_array[TID].join_id = -1;            // reset join ID
        }
        free(TCB_array[TID].stack);                 // free the stack of the thread
    }

    /* scheduling the next running thread once we are exiting */
    bool ready = false;
    int i;
    for(i = 0; i < NUM_THREADS; i++)
    {
        if(TCB_array[i].status == 1)    // if ready
        {
            ready = true;
        }
    }

    if(ready)                             // if there is a ready thread, call scheduler
    {
        round_robin_schedule();
    }

    /* cant rlly just free this anymore
    for(j = 0; j < NUM_THREADS; j++)        // free the stack of the thread, once we are out of the thread
    {
        if(TCB_array[j].status == 0)
        {
            free(TCB_array[j].stack);       // free the stack of the thread
            TCB_array[j].status = 4;        // set status to empty
        }
    }    */
   unlock();
    exit(0);
}
/* return the thread ID of the calling thread */
pthread_t pthread_self(void)
{
    return TID;
}

/* homework 3 extensions */

/* suspends execution of the calling thread until the target thread terminates unless the target thread has already terminated */
/* on return from a succcessful call with a non-NULL value_ptr arg, the value passed to pthread_exit by the terminating thread is stored in the loc referenced by value ptr
when a pthread_join retuurns successfully, the target thread has been terminated.*/
int pthread_join(pthread_t thread, void **value_ptr)
{
    lock();
    if(value_ptr == NULL)
    {
        /* undefined behavior */
    }
    else
    {
        *value_ptr = TCB_array[thread].exit_code;               // set the value of the exit code of the target thread
        if(TCB_array[thread].status == 0)                       // if the target thread is already exited, don't postpone
        {
        }
        else if(TCB_array[thread].join_id == -1)                // if the target thread has not been joined yet, it can be joined. undefined for concurrent calls with the same target thread
        {
            TCB_array[TID].status = 4;                          // set status to blocked
            TCB_array[thread].join_id = TCB_array[TID].tid;     // set the join ID to the current thread. When target exits, needs to wake uup waiting thread
            round_robin_schedule();                             // schedule the next thread
        }
    }
    unlock();
    return 0;
}

int sem_init(sem_t *sem, int pshared, unsigned value)
{
    lock();
    my_sem_t *my_sem = malloc(sizeof(my_sem_t));
    my_sem->current_value = value;
    my_sem->initalized = true;
    my_sem->head = 0;
    my_sem->tail = 0;
    *((my_sem_t **)&sem->__align) = my_sem;
    unlock();
    return 0;
}

// decrements the semaphore pointed to by sem
// if greater than zero, prooceed
// if zero, call blocks until it becomes possible to performt the decrement
int sem_wait(sem_t *sem)
{
    lock();
    my_sem_t *my_sem = *((my_sem_t **)&sem->__align);
    if(my_sem->current_value > 0)
    {
        my_sem->current_value--;
        unlock();
        return 0;
    }
    else
    {
        TCB_array[TID].status = 3;
        my_sem->queue[my_sem->tail] = TID;
        my_sem->tail = (my_sem->tail + 1) % NUM_THREADS;
        round_robin_schedule();
    }
    unlock();
    return 0;
}

int sem_post(sem_t *sem)
{
    lock();
    my_sem_t *my_sem = *((my_sem_t **)&sem->__align);
    my_sem->current_value++;
    if(my_sem->head != my_sem->tail)
    {
        TCB_array[my_sem->queue[my_sem->head]].status = 1;
        my_sem->head = (my_sem->head + 1) % NUM_THREADS;
    }
    unlock();
    return 0;
}
/* destroys the semaphore specified at the address pointed to by sem,
means that only a sesmaphore that has been initalized by sem_init should be destroyed using sem_destroy */
// 0 on success, or error -1 and errno is set to indicate the error