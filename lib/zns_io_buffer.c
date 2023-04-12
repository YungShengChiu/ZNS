#include "zns_io_buffer.h"

io_buffer_desc_t *io_buffer_desc;

io_buffer_desc_t *io_buffer_new(void)
{
    io_buffer_desc_t *desc = (io_buffer_desc_t *)calloc(1, sizeof(io_buffer_desc_t));

    if (desc)
        CIRCLEQ_INIT(&desc->buffer_head);

    return desc;
}

int io_buffer_q_init(uint32_t q_id, size_t q_depth_max, size_t buffer_max)
{
    io_buffer_entry_t *io_buffer_entry;
    CIRCLEQ_FOREACH(io_buffer_entry, &io_buffer_desc->buffer_head, io_buffer_entry_p) {
        if (q_id == io_buffer_entry->q_desc_p->q_id)
            return 1;
    }

    io_buffer_entry = (io_buffer_entry_t *)malloc(sizeof(io_buffer_entry_t));
    if (!io_buffer_entry)
        return 2;

    io_buffer_entry->io_buffer_desc_p = io_buffer_desc;
    io_buffer_entry->q_desc_p = io_buffer_q_new(q_id, q_depth_max, buffer_max);
    if (!io_buffer_entry->q_desc_p) {
        free(io_buffer_entry);
        return 3;
    }

    return 0;
}

int io_buffer_enqueue(io_buffer_entry_t *io_buffer_entry)
{
    if (!io_buffer_desc)
        return 1;
    if (!io_buffer_entry)
        return 2;
    
    io_buffer_insert_front(io_buffer_entry);
    io_buffer_desc->q_nums++;

    return 0;
}

int io_buffer_dequeue(io_buffer_entry_t **io_buffer_entry)
{
    if (!io_buffer_desc)
        return 1;
    
    *io_buffer_entry = CIRCLEQ_LAST(&io_buffer_desc->buffer_head);
    io_buffer_remove(*io_buffer_entry);
    io_buffer_desc->q_nums--;

    return 0;
}

int io_buffer_free(void)
{
    if (!io_buffer_desc)
        return 1;
    
    int rc;
    io_buffer_entry_t *io_buffer_entry;
    for (; io_buffer_desc->q_nums; free(io_buffer_entry)) {
        rc = io_buffer_dequeue(&io_buffer_entry);
        if (rc)
            return rc;

        rc = io_buffer_q_free(io_buffer_entry->q_desc_p);
        if (rc)
            return rc;
    }

    free(io_buffer_desc);
    io_buffer_desc = NULL;

    return 0;
}

//  TODO