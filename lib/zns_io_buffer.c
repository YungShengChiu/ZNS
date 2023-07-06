#include "zns_io_buffer.h"

io_buffer_desc_t *io_buffer_new(void)
{
    io_buffer_desc_t *desc = (io_buffer_desc_t *)calloc(1, sizeof(io_buffer_desc_t));
    return desc;
}

int io_buffer_init(uint32_t q_max_nums)
{
    if (!io_buffer_desc)
        return 150;
    
    io_buffer_desc->q_max_nums = q_max_nums;
    CIRCLEQ_INIT(&io_buffer_desc->buffer_head);

    io_buffer_desc->buffer_pool_p = buffer_pool_new();
    if (!io_buffer_desc->buffer_pool_p)
        return 145;
    
    return 0;
}

io_buffer_entry_t *io_buffer_q_find(uint64_t q_id)
{
    io_buffer_entry_t *io_buffer_entry = NULL;

    CIRCLEQ_FOREACH(io_buffer_entry, &io_buffer_desc->buffer_head, io_buffer_entry_p) {
        if (q_id == io_buffer_entry->q_desc_p->q_id)
            return io_buffer_entry;
    }
    return NULL;
}

int io_buffer_init_q(io_buffer_entry_t **io_buffer_entry, uint64_t q_id, size_t q_size_max)
{
    *io_buffer_entry = (io_buffer_entry_t *)malloc(sizeof(io_buffer_entry_t));
    if (!*io_buffer_entry)
        return 170;

    (*io_buffer_entry)->io_buffer_desc_p = io_buffer_desc;
    (*io_buffer_entry)->q_desc_p = q_new(*io_buffer_entry, q_id, q_size_max);
    if (!(*io_buffer_entry)->q_desc_p) {
        free(*io_buffer_entry);
        return 120;
    }

    int rc = buffer_pool_dequeue(&io_buffer_desc->buffer_pool_p->buffer_pool_free_list, 
                        &((*io_buffer_entry)->buffer_entry_p));
    if (rc) {
        q_free((*io_buffer_entry)->q_desc_p);
        free(*io_buffer_entry);
        return rc;
    }

    rc = buffer_pool_enqueue(&io_buffer_desc->buffer_pool_p->buffer_pool_allocated_list, 
                        (*io_buffer_entry)->buffer_entry_p);
    if (rc) {
        q_free((*io_buffer_entry)->q_desc_p);
        free(*io_buffer_entry);
        return rc;
    }

    return 0;
}

int io_buffer_enqueue(io_buffer_entry_t *io_buffer_entry)
{
    if (!io_buffer_desc)
        return 150;
    if (!io_buffer_entry)
        return 151;
    
    io_buffer_insert_front(io_buffer_entry);

    return 0;
}

int io_buffer_dequeue(io_buffer_entry_t **io_buffer_entry)
{
    if (!io_buffer_desc)
        return 150;
    
    if (CIRCLEQ_EMPTY(&io_buffer_desc->buffer_head))
        return 180;

    *io_buffer_entry = CIRCLEQ_LAST(&io_buffer_desc->buffer_head);
    io_buffer_remove(*io_buffer_entry);

    return 0;
}

int io_buffer_free(void)
{
    if (!io_buffer_desc)
        return 150;
    
    int rc;
    io_buffer_entry_t *io_buffer_entry;
    for (; !CIRCLEQ_EMPTY(&io_buffer_desc->buffer_head); free(io_buffer_entry)) {
        rc = io_buffer_dequeue(&io_buffer_entry);
        if (rc)
            return rc;

        rc = q_free(io_buffer_entry->q_desc_p);
        if (rc)
            return rc;
    }
    

    free(io_buffer_desc);
    io_buffer_desc = NULL;

    return 0;
}

void io_buffer_insert_front(io_buffer_entry_t *io_buffer_entry)
{
    if (!io_buffer_entry)
        return;
    
    CIRCLEQ_INSERT_HEAD(&io_buffer_desc->buffer_head, io_buffer_entry, io_buffer_entry_p);
    io_buffer_entry->io_buffer_desc_p->q_nums++;
}

void io_buffer_insert_tail(io_buffer_entry_t *io_buffer_entry)
{
    if (!io_buffer_entry)
        return;
    
    CIRCLEQ_INSERT_TAIL(&io_buffer_desc->buffer_head, io_buffer_entry, io_buffer_entry_p);
    io_buffer_entry->io_buffer_desc_p->q_nums++;
}

void io_buffer_remove(io_buffer_entry_t *io_buffer_entry)
{
    if (!io_buffer_entry)
        return;
    
    CIRCLEQ_REMOVE(&io_buffer_desc->buffer_head, io_buffer_entry, io_buffer_entry_p);
    io_buffer_entry->io_buffer_desc_p->q_nums--;
}

q_desc_t *q_new(io_buffer_entry_t *io_buffer_entry_p, uint64_t q_id, size_t size_max)
{
    q_desc_t *q_desc = (q_desc_t *)calloc(1, sizeof(q_desc_t));
    
    if (!q_desc)
        return NULL;

    q_desc->io_buffer_entry_p = io_buffer_entry_p;
    q_desc->q_id = q_id;
    q_desc->q_size_max = size_max;
    CIRCLEQ_INIT(&q_desc->q_head);
    
    return q_desc;
}

int q_enqueue(q_desc_t *q_desc, q_entry_t **q_entry, void *payload, uint32_t size)
{
    if (!q_desc)
        return 100;
    if (!payload)
        return 102;
    
    *q_entry = (q_entry_t *)malloc(sizeof(q_entry_t));
    if (!*q_entry)
        return 121;
    (*q_entry)->payload = payload;
    (*q_entry)->q_desc_p = q_desc;
    (*q_entry)->size = size;
    
    q_insert_front(*q_entry);

    return 0;
}

int q_dequeue(q_desc_t *q_desc, q_entry_t **q_entry)
{
    if (!q_desc)
        return 100;
    if (CIRCLEQ_EMPTY(&q_desc->q_head))
        return 130;
    
    *q_entry = CIRCLEQ_LAST(&q_desc->q_head);
    q_remove(*q_entry);
    
    return 0;
}

int q_free(q_desc_t *q_desc)
{
    if (!q_desc)
        return 100;
    
    int rc;
    q_entry_t *q_entry;
    for (; !CIRCLEQ_EMPTY(&q_desc->q_head); q_release_entry(q_entry)) {
        rc = q_dequeue(q_desc, &q_entry);
        if (rc)
            return rc;
    }
    
    free(q_desc);

    return 0;
}

void q_remove(q_entry_t *q_entry)
{
    if (!q_entry)
        return;
    
    CIRCLEQ_REMOVE(&q_entry->q_desc_p->q_head, q_entry, q_entry_p);
    q_entry->q_desc_p->q_size -= q_entry->size;
}

void q_insert_front(q_entry_t *q_entry)
{
    if (!q_entry)
        return;
    
    CIRCLEQ_INSERT_HEAD(&q_entry->q_desc_p->q_head, q_entry, q_entry_p);
    q_entry->q_desc_p->q_size += q_entry->size;
}

int io_buffer_reset_zone(uint64_t zslba, bool select_all)
{
    if (!io_buffer_desc)
        return 150;
    
    int rc;
    io_buffer_entry_t *io_buffer_entry;
    if (select_all) {
        for (; !CIRCLEQ_EMPTY(&io_buffer_desc->buffer_head); free(io_buffer_entry)) {
            rc = io_buffer_dequeue(&io_buffer_entry);
            if (rc)
                return rc;

            rc = q_free(io_buffer_entry->q_desc_p);
            if (rc)
                return rc;
            
            rc = buffer_pool_reset_entry(io_buffer_desc->buffer_pool_p, io_buffer_entry->buffer_entry_p);
            if (rc)
                return rc;
            
            free(io_buffer_entry);
        }

        CIRCLEQ_INIT(&io_buffer_desc->buffer_head);

    } else {
        io_buffer_entry = io_buffer_q_find(zslba);
        if (!io_buffer_entry)
            return 175;
        
        rc = q_free(io_buffer_entry->q_desc_p);
        if (rc)
            return rc;
        
        rc = buffer_pool_reset_entry(io_buffer_desc->buffer_pool_p, io_buffer_entry->buffer_entry_p);
        if (rc)
            return rc;

        io_buffer_remove(io_buffer_entry);
        free(io_buffer_entry);
    }

    return 0;
}

int io_buffer_open_zone(uint64_t zslba, bool select_all)
{
    io_buffer_entry_t *io_buffer_entry = io_buffer_q_find(zslba);
    return buffer_pool_allocate_entry(io_buffer_desc->buffer_pool_p, &io_buffer_entry);
}

int io_buffer_close_zone(uint64_t zslba, bool select_all)
{
    return 0;
}

int io_buffer_finish_zone(uint64_t zslba, bool select_all)
{
    return 0;
}

int io_buffer_offline_zone(uint64_t zslba, bool select_all)
{
    return 0;
}