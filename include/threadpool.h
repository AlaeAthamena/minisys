#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stdbool.h>

typedef void (*thread_work_func)(void *arg);

typedef struct thread_task {
    thread_work_func function;
    void *arg;
    struct thread_task *next;
} thread_task_t;

typedef struct threadpool {
    pthread_mutex_t lock;
    pthread_cond_t notify;
    pthread_t *threads;
    thread_task_t *task_queue_head;
    thread_task_t *task_queue_tail;
    int thread_count;
    int queue_size;
    bool shutdown;
} threadpool_t;

threadpool_t *threadpool_create(int num_threads);
bool threadpool_add(threadpool_t *pool, thread_work_func function, void *arg);
void threadpool_destroy(threadpool_t *pool);

#endif // THREADPOOL_H
