#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "locker.h"

static int g_count = 0;

locker mylock;

static void *thread_fun_1(void *data)
{
    mylock.lock();

    g_count++;
    printf("%s g_count: %d\n", __func__, g_count);

    mylock.unlock();
}

static void *thread_fun_2(void *data)
{
    // pthread_mutex_lock(&g_mutex_lock);

    mylock.lock();

    g_count++;
    printf("%s g_count: %d\n", __func__, g_count);

    mylock.unlock();
}

static void *thread_fun_3(void *data)
{
    mylock.lock();

    g_count++;
    printf("%s g_count: %d\n", __func__, g_count);

    mylock.unlock();
    // pthread_mutex_unlock(&g_mutex_lock);
}

int main(int argc, char const *argv[])
{
    int ret;
    pthread_t pid[3];

    if (ret != 0) {
        printf("mutex init failed\n");
        return -1;
    }

    pthread_create(&pid[0], NULL, thread_fun_1, NULL);
    pthread_create(&pid[1], NULL, thread_fun_2, NULL);
    pthread_create(&pid[2], NULL, thread_fun_3, NULL);

    pthread_join(pid[0], NULL);
    pthread_join(pid[1], NULL);
    pthread_join(pid[2], NULL);


    return 0;
}
