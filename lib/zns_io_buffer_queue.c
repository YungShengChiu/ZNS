#include "zns_io_buffer_queue.h"

io_buffer_q_desc_t *io_buffer_q_new(io_buffer_entry_t *io_buffer_entry_p, uint32_t q_id, size_t size_max)
{
    io_buffer_q_desc_t *q_desc = (io_buffer_q_desc_t *)calloc(1, sizeof(io_buffer_q_desc_t));
    
    if (!q_desc)
        return NULL;

    q_desc->io_buffer_entry_p = io_buffer_entry_p;
    q_desc->q_id = q_id;
    q_desc->q_size_max = size_max;
    CIRCLEQ_INIT(&q_desc->q_head);
    
    return q_desc;
}

int io_buffer_q_enqueue(io_buffer_q_desc_t *q_desc, void *arg, uint32_t arg_size)
{
    if (!q_desc)
        return 1;
    if (!arg)
        return 2;
    
    q_entry_t *q_entry = (q_entry_t *)malloc(sizeof(q_entry_t));
    if (!q_entry)
        return 3;
    q_entry->payload = arg;
    q_entry->q_desc_p = q_desc;
    q_entry->size = arg_size;
    
    io_buffer_q_insert_front(q_entry);
    q_desc->q_size += arg_size;

    return 0;
}

int io_buffer_q_dequeue(io_buffer_q_desc_t *q_desc, q_entry_t **q_entry)
{
    if (!q_desc)
        return 1;
    if (!q_desc->q_depth)
        return 2;
    
    *q_entry = CIRCLEQ_LAST(&q_desc->q_head);
    io_buffer_q_remove(*q_entry);
    q_desc->q_size -= q_entry->size;

    return 0;
}

void io_buffer_q_remove(q_entry_t *q_entry)
{
    if (!q_entry)
        return;
    
    CIRCLEQ_REMOVE(&q_entry->q_desc_p->q_head, q_entry, q_entry_p);
    q_entry->q_desc_p->q_size -= q_entry->size;
}

void io_buffer_q_insert_front(q_entry_t *q_entry)
{
    if (!q_entry)
        return;
    
    CIRCLEQ_INSERT_HEAD(&q_entry->q_desc_p->q_head, q_entry, q_entry_p);
    q_entry->q_desc_p->q_size += q_entry->size;
}

int io_buffer_q_free(io_buffer_q_desc_t *q_desc)
{
    if (!q_desc)
        return -1;
    
    int rc;
    q_entry_t *q_entry;
    for (; q_desc->q_depth; spdk_free(io_buffer_q_release_entry(q_entry))) {
        rc = io_buffer_q_dequeue(q_desc, &q_entry);
        if (rc)
            return rc;
    }
    
    free(q_desc);

    return 0;
}