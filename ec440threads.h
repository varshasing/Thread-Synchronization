#ifndef __EC440THREADS__
#define __EC440THREADS__

#define NUM_THREADS 128         // maximum number of threads alive at the same time

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>

/* given function for demangling*/
unsigned long int ptr_demangle(unsigned long int p)
{
    unsigned long int ret;

    asm("movq %1, %%rax;\n"
        "rorq $0x11, %%rax;"
        "xorq %%fs:0x30, %%rax;"
        "movq %%rax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%rax"
    );
    return ret;
}
/* given function for mangling*/
unsigned long int ptr_mangle(unsigned long int p)
{
    unsigned long int ret;

    asm("movq %1, %%rax;\n"
        "xorq %%fs:0x30, %%rax;"
        "rolq $0x11, %%rax;"
        "movq %%rax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%rax"
    );
    return ret;
}

/* obtaining the return value of a thread that does not call pthread_exit explicitly thru return value of thread's start function */
void pthread_exit_wrapper()
{
    unsigned long int res;
    asm("movq %%rax, %0\n":"=r"(res));
    pthread_exit((void *) res);
}


void *start_thunk() {
  asm("popq %%rbp;\n"           //clean up the function prolog
      "movq %%r13, %%rdi;\n"    //put arg in $rdi
      "pushq %%r12;\n"          //push &start_routine
      "retq;\n"                 //return to &start_routine
      :
      :
      : "%rdi"
  );
  __builtin_unreachable();
}

// different thread status:
// exit, ready, running, blocked, empty

/* thread control block struct */
typedef struct TCB{
    pthread_t tid;                  // thread ID, needed for scheduling
    void *stack;
    jmp_buf registers;              // register values for the thread (env)
    int status;
    void* exit_code;                // exit code of the thread (address pointer)
    pthread_t join_id;              // thread ID of the thread that is being joined, need for keeping track of blocked
} TCB;

/* create own semaphore structure that stores the current value, a pointer to a queue for threads that are waiting, and a flag that indicates whether the semaphor is initalized*/
typedef struct my_sem_t{
    //sem_t* semaphore;
    int current_value;
    int queue[NUM_THREADS];         // queue for threads that are waiting. max number of threads waiting is max threads...?
    bool initalized;                // initalized?
    int head;                       // head of the queue -- circular queue
    int tail;                       // tail of queue
    int size;                       // size of queue
} my_sem_t;

void round_robin_schedule();        // schedule the next thread to run, 50ms quantum

void first_time();                  // used when the application calls pthread_create for the first time, transition to multithreaded system

int pthread_create(                 // create a new thread within a proces. Upon successful completion,
    pthread_t *thread,
    const pthread_attr_t *attr,
    void *(*start_routine)(void *),
    void *arg
);

void pthread_exit(void *value_ptr); // terminates the calling thread. ignore value passed in as the first argument and clean up all information related to t terminating thread.

pthread_t pthread_self(void);       // return the thread ID of the calling thread

/* homework 3 extensions */

/* sigprocmask to ensure that the current thread can no longer be interupted by alarm */
void lock()
{
    sigset_t set;
    sigemptyset(&set);                      // empty set
    sigaddset(&set, SIGALRM);               // adding SIGALRM to set
    sigprocmask(SIG_BLOCK, &set, NULL);     // block SIGARLM
}

/* once a thread calls unlock, the thread resumes its normal status and will be scheduled whenever an alarm signal is recieved */
void unlock()
{
    sigset_t set;
    sigemptyset(&set);                      // empty set
    sigaddset(&set, SIGALRM);               // adding SIGALRM to set
    sigprocmask(SIG_UNBLOCK, &set, NULL);   // unblock SIGARLM
}

/* postpone the execution of the thread that INITIATED The call until the target thread terminates, unless the target thread has already terminated */
/* correctly handle the exit code of threads that terminate */
int pthread_join(pthread_t thread, void **value_ptr);

/* initalize unnamed sephatore referred to by sem. pshared argument ALWAYS equals 0
means that the semaphore pointed to by sem is shared between threads of the process.
Attempting to initalize an already initalized sephamore results in undefined behavior */
int sem_init(sem_t *sem, int pshared, unsigned int value);

/* deecrements the semaphore pointed to by sem. If the semaphore's value is greater than zero,
then the decrement proceeds, and the function returns immediately. If the semaphor currently has
the value zero, then the call BLOCKS until it becomes possible to perform the decrement
(the semaphore value rises above zero). Value of the semaphore NEVER falls below zero.*/
int sem_wait(sem_t *sem);

/* increments semaphore pointed to by sem. If the value becomes consequently greater than zero,
another thread blocked in a sem_wait call will be woken up and proceeds to lock the semaphore
When a thread is woken up and takes the lock as part of sem_post, the value of semaphore remains zero*/
int sem_post(sem_t *sem);

/* destroys the semaphore specified at the address pointed to by sem, only a semaphore that has been
initalized by sem_init should be destroyed using sem_destroy. Destroying a semaphore that other threads
are currently blocked on (in sem_wait) produced undefined behavior.
Using a semaphore that has been destroyed produced undefined results, until the semaphore has been reinitalized using sem_init.*/
int sem_destroy(sem_t *sem);

#endif
