Homework 3

The project goals for this homework was to:
1. Enable numerous threads to safely interact
2. Extend our threading library with basic synchronization primitives

To create a locking mechanism that switches the interativity of a thread on/off, we had to create two functions: lock and unlock. When a thread calls lock, it can no longer be interrupted by any other thread. When it calls unlock, the thread resumts its normal status and will be scheduled whenever an alarm signal is recieved.

# implementing lock and unlock
    the way I went about implementing these was to block the alarms from the previously made signal handler, used for maintaining a preemptive setup. The way I went about these were by creating an empty signal set and doing a union between it and the SIGARLMm using sigprocmask for SIG_BLOCK and SIG_UNBLOCK. I called lock and unlock at the start and end of the folowing functions, to prevent switching processes while within a semaphore logic or pthread logic:
    1. pthread_create
    2. pthread_exit
    3. pthread_join
    4. sem_init
    5. sem_wait
    6. sem_post
    7. sem_destroy

# implementing pthread_join
This function will postpone the execution of the thread that initiated the call until the target thread terminates, unless the target thread has already terminated. There is now a need to correctly handle the exit code of threads that terminated. On return from a successful pthread_join call with a non_NULL value_ptr argument, the value passed to pthread_exit by the terminting thread shall be made available in the location referenced by value_ptr.
    for implementing pthread join, I needed to add a some changes to my TCB struct previously created for the second homework. I had to add an exit code and properly use it for this homework, as well as using the BLOCKED status for a thread. The function itself has undefined behavio for if value_ptr is NULL, so I did not do anything if that was the case. Otherwise, I had to collect the exit code of the thread that terminates, which is the target thread specified in the function call. I assign the exit value to the passed address of the *value_ptr argument, and then check if the thread I would like to join has no other threads waiting for it. If that is the cae, I set the thread that would like to join to be blocked, and store the thread's id in the join_id field of the target thread. After this, I call my scheduler again, as the calling thread is now blocked and should not be running until the target thread exits.

# changes to pthread_exit
    I had some changes for this function, because I can no longer mass-free all of my exited thread's stacks. Insteda, I had to check if the calling thead is exited, and set the exit code to the value pointer passed in as an argument. It is important to check that the join_id has no threads waiting for it, as we need to wake up those threads and set them to ready if that is the case. Because multiple concurrent calls with the same target in pthread_join is undefined, I was able to just set the exiting thread's join_id thread to be set to ready, and free the memory at the end of that block. At the end, I run through all of the different threads and check to see if any of them are ready, if that is the case, I call my scheduler to pick up the next thread.

# changes to pthread_create
    I had to slightly change some logic here, as I need to now set the join_id value for each thread, as well as setting the exit code to NULL to intalize the empty TCB_array. The other thing that was required to change for this function was the process of setting the exit function to the stack in order to make sure that it knew where to return to. I used the pthread_exit_wrapper to implement this.

for the semaphores, I had to create a semaphore struct to make more data accessible than the struct originally holds. we allocate space for this in the __align member.
I init semaphore by setting the value, bool initalized, and use a queue to maintain what is waiting on the semaphore.
I wait on the semaphore by decrementing the value, and if it is less than 0, I add the thread to the queue and block it.
I post on the semaphore by incrementing the value, and if the queue is not done, I set it to ready and call the scheduler and move the head.
I destroy the semaphore by freeing the memory allocated for the semaphore.