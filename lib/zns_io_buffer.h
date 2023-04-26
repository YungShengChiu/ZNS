#include "zns_io_buffer_queue.h"

/*
 *      The io_buffer_queue is implemented by Linux CIRCLEQ
 **/

#ifndef ZNS_IO_BUFFER_H
#define ZNS_IO_BUFFER_H

extern io_buffer_desc_t *io_buffer_desc;

typedef struct io_buffer_entry_t io_buffer_entry_t;
typedef struct io_buffer_desc_t io_buffer_desc_t;

struct io_buffer_entry_t {
    CIRCLEQ_ENTRY(io_buffer_entry_t) io_buffer_entry_p;
    io_buffer_desc_t *io_buffer_desc_p;
    io_buffer_q_desc_t *q_desc_p;
};

CIRCLEQ_HEAD(buffer_head_t, io_buffer_entry_t);
struct io_buffer_desc_t {
    struct buffer_head_t buffer_head;
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_nvme_qpair *qpair;
    pthread_mutex_t io_buffer_mutex;
    uint16_t q_nums;
    uint16_t q_max_nums;
};

io_buffer_desc_t *io_buffer_new(void);

io_buffer_entry_t *io_buffer_q_find(io_buffer_entry_t **io_buffer_entry, uint32_t q_id);

int io_buffer_q_init(io_buffer_entry_t **io_buffer_entry, uint32_t q_id, size_t q_size_max);

int io_buffer_enqueue(io_buffer_entry_t *io_buffer_entry);

int io_buffer_dequeue(io_buffer_entry_t **io_buffer_entry);

int io_buffer_free(void);

void io_buffer_insert_front(io_buffer_entry_t *io_buffer_entry);

void io_buffer_remove(io_buffer_entry_t *io_buffer_entry);

int io_buffer_reset_zone(uint32_t q_id, bool select_all);

//  TODO

#endif