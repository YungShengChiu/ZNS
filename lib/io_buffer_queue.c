#include "io_buffer_queue.h"

io_buffer_q_desc_t *io_buffer_q_new(uint32_t q_id, size_t q_depth_max, size_t buffer_max)
{
    io_buffer_q_desc_t *q_desc = (io_buffer_q_desc_t *)calloc(1, sizeof(io_buffer_q_desc_t));
    
    if (!q_desc)
        return NULL;

    q_desc->q_id = q_id;
    q_desc->q_depth_max = q_depth_max;
    q_desc->q_buffer_max = buffer_max;
    CIRCLEQ_INIT(&q_desc->q_head);
    
    return q_desc;
}

int io_buffer_q_enqueue(io_buffer_q_desc_t *q_desc, void *arg)
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
    
    CIRCLEQ_INSERT_HEAD(&q_desc->q_head, q_entry, q_entry_p);
    q_desc->q_depth++;
    return 0;
}

int io_buffer_q_dequeue(io_buffer_q_desc_t *q_desc, q_entry_t **q_entry)
{
    if (!q_desc)
        return 1;
    if (CIRCLEQ_EMPTY(&q_desc->q_head))
        return 2;
    
    *q_entry = CIRCLEQ_LAST(&q_desc->q_head);
    io_buffer_q_remove(*q_entry);
    q_desc->q_depth--;
    return 0;
}

int io_buffer_q_free(io_buffer_q_desc_t *q_desc)
{
    if (!q_desc)
        return -1;
    
    int rc;
    for (q_entry_t *q_entry; !CIRCLEQ_EMPTY(&q_desc->q_head); spdk_free(io_buffer_q_release_entry(q_entry))) {
        rc = io_buffer_q_dequeue(q_desc, &q_entry);
        if (rc)
            return rc;
    }
    
    free(q_desc);
    return 0;
}