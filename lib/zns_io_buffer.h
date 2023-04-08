#include "io_buffer_queue.h"

#ifndef IO_BUFFER_H
#define IO_BUFFER_H

typedef struct io_buffer_q_t io_buffer_q_t;
typedef struct io_buffer_desc_t io_buffer_desc_t;

struct io_buffer_q_t {
    CIRCLEQ_ENTRY(io_buffer_q_t) io_buffer_q_p;
    io_buffer_desc_t *io_buffer_desc_p;
    io_buffer_q_desc_t *q_desc_p;
};

CIRCLEQ_HEAD(buffer_head_t, io_buffer_q_t);
struct io_buffer_desc_t {
    struct buffer_head_t buffer_head;
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_nvme_qpair *qpair;
    uint8_t q_nums;
    uint8_t q_max_nums;
};

//  TODO

#endif