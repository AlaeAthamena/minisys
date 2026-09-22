#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>

static void *threadpool_worker(void *arg) {
    threadpool_t *pool = (threadpool_t *)arg;

    while (1) {
        pthread_mutex_lock(&(pool->lock));

        while ((pool->queue_size == 0) && (!pool->shutdown)) {
            pthread_cond_wait(&(pool->notify), &(pool->lock));
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&(pool->lock));
            pthread_exit(NULL);
        }

        thread_task_t *task = pool->task_queue_head;
        if (task) {
            pool->task_queue_head = task->next;
            if (pool->task_queue_head == NULL) {
                pool->task_queue_tail = NULL;
            }
            pool->queue_size--;
        }

        pthread_mutex_unlock(&(pool->lock));

        if (task) {
            (*(task->function))(task->arg);
            free(task);
        }
    }

    return NULL;
}

threadpool_t *threadpool_create(int num_threads) {
    if (num_threads <= 0) num_threads = 4;

    threadpool_t *pool = calloc(1, sizeof(threadpool_t));
    if (!pool) return NULL;

    pool->thread_count = num_threads;
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    pthread_mutex_init(&(pool->lock), NULL);
    pthread_cond_init(&(pool->notify), NULL);

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&(pool->threads[i]), NULL, threadpool_worker, (void *)pool) != 0) {
            threadpool_destroy(pool);
            return NULL;
        }
    }

    return pool;
}

bool threadpool_add(threadpool_t *pool, thread_work_func function, void *arg) {
    if (!pool || !function) return false;

    pthread_mutex_lock(&(pool->lock));

    if (pool->shutdown) {
        pthread_mutex_unlock(&(pool->lock));
        return false;
    }

    thread_task_t *task = malloc(sizeof(thread_task_t));
    if (!task) {
        pthread_mutex_unlock(&(pool->lock));
        return false;
    }

    task->function = function;
    task->arg = arg;
    task->next = NULL;

    if (pool->task_queue_tail) {
        pool->task_queue_tail->next = task;
        pool->task_queue_tail = task;
    } else {
        pool->task_queue_head = task;
        pool->task_queue_tail = task;
    }
    pool->queue_size++;

    pthread_cond_signal(&(pool->notify));
    pthread_mutex_unlock(&(pool->lock));

    return true;
}

void threadpool_destroy(threadpool_t *pool) {
    if (!pool) return;

    pthread_mutex_lock(&(pool->lock));
    pool->shutdown = true;
    pthread_cond_broadcast(&(pool->notify));
    pthread_mutex_unlock(&(pool->lock));

    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);

    thread_task_t *curr = pool->task_queue_head;
    while (curr) {
        thread_task_t *next = curr->next;
        free(curr);
        curr = next;
    }

    pthread_mutex_destroy(&(pool->lock));
    pthread_cond_destroy(&(pool->notify));
    free(pool);
}
