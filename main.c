#include <pthread.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <semaphore.h>

#define THREAD_CNT 15


sem_t newSem;

void *count(void *arg){

    printf("Thread %lu wants the lock\n", (unsigned long)pthread_self());
    sem_wait(&newSem);
    printf("Thread %lu has been given the lock\n", (unsigned long)pthread_self());
    long i;
    for(i = 0; i < 3; i++){
        printf("Thread %lu currently holds the lock\n", (unsigned long)pthread_self());
        pause();
    }

    printf("Thread %lu is done waisting time\n", (unsigned long)pthread_self());
    sem_post(&newSem);
    printf("Thread %lu no longer holds the lock\n", (unsigned long)pthread_self());

  return arg;
}

int main(int argc, char **argv){
        pthread_t threads[THREAD_CNT];

    sem_init(&newSem, 0, 1);

    int i;
    for(i = 0; i<THREAD_CNT; i++)
    {
        pthread_create(&threads[i], NULL, count, &(newSem));
    }

     for(i = 0; i<THREAD_CNT; i++) {
        printf("Join %d returns %d\n", i, pthread_join(threads[i], NULL));
    }


    return 0;
}