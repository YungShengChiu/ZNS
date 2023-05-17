#include <spdk/stdinc.h>
#include <spdk/queue.h>
#include <spdk/env.h>

/*
 *      The zns_io_buffer is implemented by Linux CIRCLEQ
 **/

#ifndef ZNS_IO_BUFFER_H
#define ZNS_IO_BUFFER_H

typedef struct io_buffer_entry_t io_buffer_entry_t;
typedef struct io_buffer_desc_t io_buffer_desc_t;
typedef struct q_entry_t q_entry_t;
typedef struct q_desc_t q_desc_t;

CIRCLEQ_HEAD(buffer_head_t, io_buffer_entry_t);
struct io_buffer_desc_t {
    struct buffer_head_t buffer_head;
    uint32_t q_nums;
    uint32_t q_max_nums;
};

struct io_buffer_entry_t {
    CIRCLEQ_ENTRY(io_buffer_entry_t) io_buffer_entry_p;
    io_buffer_desc_t *io_buffer_desc_p;
    q_desc_t *q_desc_p;
};

CIRCLEQ_HEAD(q_head_t, q_entry_t);
struct q_desc_t {
    struct q_head_t q_head;
    io_buffer_entry_t *io_buffer_entry_p;
    uint64_t q_id;
    size_t q_size;
    size_t q_size_max;
};

struct q_entry_t {
    CIRCLEQ_ENTRY(q_entry_t) q_entry_p;
    q_desc_t *q_desc_p;
    void *payload;
    uint32_t size;
};

extern io_buffer_desc_t *io_buffer_desc;

io_buffer_desc_t *io_buffer_new(void);

int io_buffer_init(uint32_t q_max_nums);

io_buffer_entry_t *io_buffer_q_find(uint64_t q_id);

inline io_buffer_entry_t *io_buffer_q_last(void)
{
    return CIRCLEQ_LAST(&io_buffer_desc->buffer_head);
}

int io_buffer_init_q(io_buffer_entry_t **io_buffer_entry, uint64_t q_id, size_t q_size_max);

int io_buffer_enqueue(io_buffer_entry_t *io_buffer_entry);

int io_buffer_dequeue(io_buffer_entry_t **io_buffer_entry);

int io_buffer_free(void);

void io_buffer_insert_front(io_buffer_entry_t *io_buffer_entry);

void io_buffer_insert_tail(io_buffer_entry_t *io_buffer_entry);

void io_buffer_remove(io_buffer_entry_t *io_buffer_entry);

q_desc_t *q_new(io_buffer_entry_t *io_buffer_entry_p, uint64_t q_id, size_t size_max);

int q_enqueue(q_desc_t *q_desc, q_entry_t **q_entry, void *payload, uint32_t size);

int q_dequeue(q_desc_t *q_desc, q_entry_t **q_entry);

int q_free(q_desc_t *q_desc);

inline void *q_release_entry(q_entry_t *q_entry)
{
    void *payload = q_entry->payload;
    free(q_entry);
    return payload;
}

void q_remove(q_entry_t *q_entry);

void q_insert_front(q_entry_t *q_entry);

int io_buffer_reset_zone(uint64_t zslba, bool select_all);

int io_buffer_open_zone(uint64_t zslba, bool select_all);

int io_buffer_close_zone(uint64_t zslba, bool select_all);

int io_buffer_finish_zone(uint64_t zslba, bool select_all);

int io_buffer_offline_zone(uint64_t zslba, bool select_all);

#endif