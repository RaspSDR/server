#pragma once
#include <stdint.h>

typedef void *gpointer;
struct _GAsyncQueue;
typedef struct _GAsyncQueue GAsyncQueue;
typedef int32_t gint;

#ifdef __cplusplus
extern "C" {
#endif
    GAsyncQueue *g_async_queue_new(void);
    void g_async_queue_push(GAsyncQueue *queue, gpointer data);
    void g_async_queue_push_front(GAsyncQueue *queue, gpointer data);
    gint g_async_queue_length(GAsyncQueue *queue);
    gpointer g_async_queue_pop(GAsyncQueue *queue);
    gpointer g_async_queue_try_pop(GAsyncQueue *queue);
    void g_async_queue_lock(GAsyncQueue *queue);
    void g_async_queue_unlock(GAsyncQueue *queue);
    gint g_async_queue_length_unlocked(GAsyncQueue *queue);
    void g_async_queue_unref(GAsyncQueue *queue);
    gpointer g_async_queue_pop_unlocked (GAsyncQueue *queue);
#ifdef __cplusplus
}
#endif