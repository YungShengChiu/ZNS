#include "zns_io_buffer.h"

io_buffer_desc_t *io_buffer_desc;

io_buffer_desc_t *io_buffer_new(void)
{
    io_buffer_desc_t *desc = (io_buffer_desc_t *)calloc(1, sizeof(io_buffer_desc_t));

    if (desc)
        CIRCLEQ_INIT(&desc->buffer_head);

    return desc;
}

int io_buffer_q_find_or_init(io_buffer_entry_t **io_buffer_entry, uint32_t q_id, size_t q_depth_max, size_t buffer_max)
{
    CIRCLEQ_FOREACH(*io_buffer_entry, &io_buffer_desc->buffer_head, io_buffer_entry_p) {
        if (q_id == *io_buffer_entry->q_desc_p->q_id)
            return 1;
    }

    *io_buffer_entry = (io_buffer_entry_t *)malloc(sizeof(io_buffer_entry_t));
    if (!io_buffer_entry)
        return 2;

    *io_buffer_entry->io_buffer_desc_p = io_buffer_desc;
    *io_buffer_entry->q_desc_p = io_buffer_q_new(*io_buffer_entry, q_id, q_depth_max, buffer_max);
    if (!*io_buffer_entry->q_desc_p) {
        free(*io_buffer_entry);
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

int io_buffer_reset_zone(uint32_t q_id, bool select_all)
{
    if (!io_buffer_desc)
        return 1;
    
    int rc;
    io_buffer_entry_t *io_buffer_entry = NULL;
    if (select_all) {
        for (; io_buffer_desc->q_nums; free(io_buffer_entry)) {
            rc = io_buffer_dequeue(&io_buffer_entry);
            if (rc)
                return rc;

            rc = io_buffer_q_free(io_buffer_entry->q_desc_p);
            if (rc)
                return rc;
        }

        CIRCLEQ_INIT(&io_buffer_desc->buffer_head);
    } else {
        CIRCLEQ_FOREACH(io_buffer_entry, &io_buffer_desc->buffer_head, io_buffer_entry_p) {
            if (q_id == io_buffer_entry->q_desc_p->q_id)
                break;
        }

        if (!io_buffer_entry)
            return 2;
        
        rc = io_buffer_q_free(io_buffer_entry->q_desc_p);
        if (rc)
            return rc;
        
        io_buffer_remove(io_buffer_entry);
        free(io_buffer_entry);
    }

    return 0;
}
/*
int io_buffer_wb_zone(io_buffer_entry_t *io_buffer_entry, uint32_t nr_blocks)
{
    if (!io_buffer_desc)
        return 1;
    
    if (!io_buffer_entry)
        return 2;
    
    uint64_t zslba = io_buffer_entry->q_desc_p->q_id * nr_blocks;
    io_buffer_q_desc_t *q_desc = io_buffer_entry->q_desc_p;
    q_entry_t *q_entry = NULL;
    int rc;
    for (; q_desc->q_depth;) {
        io_buffer_q_dequeue(q_desc, &q_entry);
        rc = spdk_nvme_zns_zone_append(io_buffer_desc->ns, io_buffer_desc->qpair, q_entry->payload, zslba, q_entry->size, );
    }

    int rc = io_buffer_q_free(io_buffer_entry->q_desc_p);

    return 0;
}
*/
//  TODO