#include <spdk/stdinc.h>
#include <spdk/queue.h>
#include <spdk/env.h>

/*
 *      The io_buffer_queue is implemented by Linux CIRCLEQ
 **/

#ifndef ZNS_IO_BUFFER_QUEUE_H
#define ZNS_IO_BUFFER_QUEUE_H

typedef struct q_entry_t q_entry_t;
typedef struct io_buffer_q_desc_t io_buffer_q_desc_t;

struct q_entry_t {
    CIRCLEQ_ENTRY(q_entry_t) q_entry_p;
    io_buffer_q_desc_t *q_desc_p;
    void *payload;
    uint32_t size;
};

CIRCLEQ_HEAD(q_head_t, q_entry_t);
struct io_buffer_q_desc_t {
    struct q_head_t q_head;
    io_buffer_entry_t *io_buffer_entry_p;
    uint32_t q_id;
    size_t q_size;
    size_t q_size_max;
    uint64_t z_wp;
};


io_buffer_q_desc_t *io_buffer_q_new(io_buffer_entry_t *io_buffer_entry_p, uint32_t q_id, size_t size_max);

int io_buffer_q_enqueue(io_buffer_q_desc_t *q_desc, void *arg, uint32_t arg_size);

int io_buffer_q_dequeue(io_buffer_q_desc_t *q_desc, q_entry_t **q_entry);

int io_buffer_q_free(io_buffer_q_desc_t *q_desc);

static inline void *io_buffer_q_release_entry(q_entry_t *q_entry)
{
    void *entry_payload = q_entry->payload;
    free(q_entry);
    return entry_payload;
}

static inline void io_buffer_q_remove(q_entry_t *q_entry)
{
    CIRCLEQ_REMOVE(&q_entry->q_desc_p->q_head, q_entry, q_entry_p);
}

static inline void io_buffer_q_insert_front(q_entry_t *q_entry)
{
    CIRCLEQ_INSERT_HEAD(&q_entry->q_desc_p->q_head, q_entry, q_entry_p);
}

#endif