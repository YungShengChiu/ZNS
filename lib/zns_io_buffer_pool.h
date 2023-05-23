#include <spdk/queue.h>
#include <spdk/env.h>

#ifndef ZNS_IO_BUFFER_POOL_H_
#define ZNS_IO_BUFFER_POOL_H_

typedef struct buffer_pool_t buffer_pool_t;
typedef struct buffer_pool_entry_t buffer_pool_entry_t;

CIRCLEQ_HEAD(buffer_pool_head_t, buffer_pool_entry_t);
struct buffer_pool_t {
    struct buffer_pool_head_t buffer_pool_free_list;
    struct buffer_pool_head_t buffer_pool_allocated_list;
};

struct buffer_pool_entry_t {
    CIRCLEQ_ENTRY(buffer_pool_entry_t) buffer_pool_entry_p;
    size_t buffer_size;
    void *buffer_p;
    void *sp;
};

buffer_pool_t *buffer_pool_new(void);

int buffer_pool_init(buffer_pool_t *buffer_pool, uint32_t buffer_count, size_t buffer_size, size_t align);

int buffer_pool_enqueue(struct buffer_pool_head_t *buffer_pool_head, buffer_pool_entry_t *buffer_pool_entry);

int buffer_pool_dequeue(struct buffer_pool_head_t *buffer_pool_head, buffer_pool_entry_t **buffer_pool_entry);

int buffer_pool_free(buffer_pool_t *buffer_pool);

void buffer_pool_insert_front(struct buffer_pool_head_t *buffer_pool_head, buffer_pool_entry_t *buffer_pool_entry);

void buffer_pool_remove(struct buffer_pool_head_t *buffer_pool_head, buffer_pool_entry_t *buffer_pool_entry);

int buffer_pool_reset_entry(buffer_pool_t *buffer_pool, buffer_pool_entry_t *buffer_pool_entry);

int buffer_pool_allocate_entry(buffer_pool_t *buffer_pool, buffer_pool_entry_t **buffer_pool_entry);

#endif