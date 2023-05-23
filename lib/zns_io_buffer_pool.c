#include "zns_io_buffer_pool.h"

buffer_pool_t *buffer_pool_new(void)
{
    buffer_pool_t *buffer_pool = (buffer_pool_t *)calloc(1, sizeof(buffer_pool_t));
    return buffer_pool;
}

int buffer_pool_init(buffer_pool_t *buffer_pool, uint32_t buffer_count, size_t buffer_size, size_t align)
{
    if (!buffer_pool)
        return 140;
    
    CIRCLEQ_INIT(&buffer_pool->buffer_pool_free_list);
    CIRCLEQ_INIT(&buffer_pool->buffer_pool_allocated_list);

    buffer_pool_entry_t *buffer_pool_entry = NULL;
    for (uint32_t i = 0; i < buffer_count; i++) {
        buffer_pool_entry = (buffer_pool_entry_t *)malloc(sizeof(buffer_pool_entry_t));
        if (!buffer_pool_entry)
            return 146;
        
        buffer_pool_entry->buffer_p = spdk_dma_malloc(buffer_size, align, NULL);
        if (!buffer_pool_entry->buffer_p) {
            free(buffer_pool_entry);
            return 147;
        }

        buffer_pool_entry->sp = buffer_pool_entry->buffer_p;
        buffer_pool_entry->buffer_size = buffer_size;
        
        buffer_pool_enqueue(&buffer_pool->buffer_pool_free_list, buffer_pool_entry);
    }

    return 0;
}

int buffer_pool_enqueue(struct buffer_pool_head_t *buffer_pool_head, buffer_pool_entry_t *buffer_pool_entry)
{
    if (!buffer_pool_head)
        return 140;
    
    if (!buffer_pool_entry)
        return 141;
    
    buffer_pool_insert_front(buffer_pool_head, buffer_pool_entry);

    return 0;
}

int buffer_pool_dequeue(struct buffer_pool_head_t *buffer_pool_head, buffer_pool_entry_t **buffer_pool_entry)
{
    if (!buffer_pool_head)
        return 140;
    
    if (CIRCLEQ_EMPTY(buffer_pool_head))
        return 149;
    
    *buffer_pool_entry = CIRCLEQ_LAST(buffer_pool_head);
    buffer_pool_remove(buffer_pool_head, *buffer_pool_entry);
    return 0;
}

int buffer_pool_free(buffer_pool_t *buffer_pool)
{
    if (!buffer_pool)
        return 140;

    int rc;
    buffer_pool_entry_t *buffer_pool_entry;
    struct buffer_pool_head_t *buffer_pool_head = &buffer_pool->buffer_pool_free_list;
    for (; !CIRCLEQ_EMPTY(buffer_pool_head); free(buffer_pool_entry)) {
        rc = buffer_pool_dequeue(buffer_pool_head, &buffer_pool_entry);
        if (rc)
            return rc;
        
        spdk_free(buffer_pool_entry->buffer_p);
    }
    
    buffer_pool_head = &buffer_pool->buffer_pool_allocated_list;
    for (; !CIRCLEQ_EMPTY(buffer_pool_head); free(buffer_pool_entry)) {
        rc = buffer_pool_dequeue(buffer_pool_head, &buffer_pool_entry);
        if (rc)
            return rc;
        
        spdk_free(buffer_pool_entry->buffer_p);
    }
    
    free(buffer_pool);

    return 0;
}

void buffer_pool_insert_front(struct buffer_pool_head_t *buffer_pool_head, buffer_pool_entry_t *buffer_pool_entry)
{
    if (!buffer_pool_head || !buffer_pool_entry)
        return;
    
    CIRCLEQ_INSERT_HEAD(buffer_pool_head, buffer_pool_entry, buffer_pool_entry_p);
}

void buffer_pool_remove(struct buffer_pool_head_t *buffer_pool_head, buffer_pool_entry_t *buffer_pool_entry)
{
    if (!buffer_pool_head || !buffer_pool_entry)
        return;
    
    CIRCLEQ_REMOVE(buffer_pool_head, buffer_pool_entry, buffer_pool_entry_p);
}

int buffer_pool_reset_entry(buffer_pool_t *buffer_pool, buffer_pool_entry_t *buffer_pool_entry)
{
    if (!buffer_pool)
        return 140;
    
    if (!buffer_pool_entry)
        return 141;
    
    buffer_pool_remove(&buffer_pool->buffer_pool_allocated_list, buffer_pool_entry);
    buffer_pool_entry->sp = buffer_pool_entry->buffer_p;
    memset(buffer_pool_entry->buffer_p, 0, buffer_pool_entry->buffer_size);
    buffer_pool_enqueue(&buffer_pool->buffer_pool_free_list, buffer_pool_entry);
    
    return 0;
}

int buffer_pool_allocate_entry(buffer_pool_t *buffer_pool, buffer_pool_entry_t **buffer_pool_entry)
{
    if (!buffer_pool)
        return 140;
    
    int rc = buffer_pool_dequeue(&buffer_pool->buffer_pool_free_list, buffer_pool_entry);
    if (rc) {
        *buffer_pool_entry = NULL;
        return rc;
    }

    rc = buffer_pool_enqueue(&buffer_pool->buffer_pool_allocated_list, *buffer_pool_entry);
    if (rc)
        return rc;
    
    return 0;
}