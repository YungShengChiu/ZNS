#include "io_buffer.h"

static io_buffer_desc_t *io_buffer_new()
{
    io_buffer_desc_t *desc = (io_buffer_desc_t *)calloc(1, sizeof(io_buffer_desc_t));

    if (desc)
        CIRCLEQ_INIT(&desc->buffer_head);

    return desc;
}

//  TODO