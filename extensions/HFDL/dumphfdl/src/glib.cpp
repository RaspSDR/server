#include "glib.h"
#include <pthread.h>
#include <queue>
#include <atomic>

#define MAX_QUEUE 1024

struct _GAsyncQueue
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    std::atomic<int> ref;
    std::atomic<int> wait_count;
    std::deque<gpointer> data;
};

GAsyncQueue *g_async_queue_new(void)
{
    GAsyncQueue *queue = new GAsyncQueue;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
    queue->ref = 1;

    return queue;
}

void g_async_queue_push(GAsyncQueue *queue, gpointer data)
{
    pthread_mutex_lock(&queue->mutex);
    queue->data.push_back(data);
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

void g_async_queue_push_front(GAsyncQueue *queue, gpointer data)
{
    pthread_mutex_lock(&queue->mutex);
    queue->data.push_front(data);
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

int g_async_queue_length(GAsyncQueue *queue)
{
    int len;
    pthread_mutex_lock(&queue->mutex);
    len = queue->data.size();
    pthread_mutex_unlock(&queue->mutex);

    return len;
}

gpointer g_async_queue_pop(GAsyncQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    gpointer data = g_async_queue_pop_unlocked(queue);
    pthread_mutex_unlock(&queue->mutex);

    return data;
}
gpointer g_async_queue_try_pop(GAsyncQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    if (!queue->data.empty())
    {
        gpointer data = queue->data.front();
        queue->data.pop_front();
        pthread_mutex_unlock(&queue->mutex);
        return data;
    }
    else
    {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
}
void g_async_queue_lock(GAsyncQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
}
void g_async_queue_unlock(GAsyncQueue *queue)
{
    pthread_mutex_unlock(&queue->mutex);
}
int g_async_queue_length_unlocked(GAsyncQueue *queue)
{
    int len;
    len = queue->data.size() - queue->wait_count;
    return len;
}

void g_async_queue_unref(GAsyncQueue *queue)
{
    if (queue->ref-- == 0)
    {
        pthread_mutex_destroy(&queue->mutex);
        pthread_cond_destroy(&queue->cond);
        delete queue;
    }
}

gpointer g_async_queue_pop_unlocked (GAsyncQueue *queue)
{
    while (queue->data.empty())
    {
        queue->wait_count++;
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    gpointer data = queue->data.front();
    queue->data.pop_front();

    return data;
}