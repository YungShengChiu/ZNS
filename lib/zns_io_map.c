include "zns_io_map.h"

io_map_desc_t *io_map_desc;

io_map_desc_t *io_map_new(void)
{
    io_map_desc_t *desc = (io_map_desc_t *)calloc(1, sizeof(io_map_desc_t));
    return desc;
}

int io_map_free(void)
{
    if (!io_map_desc)
        return 1;
    
    if (io_map_desc->io_map)
        free(io_map_desc->io_map);
    
    free(io_map_desc);
    
    // TODO

    return 0;
}

int io_map_init(size_t io_map_size, size_t zone_size)
{
    if (!io_map_desc)
        return 1;

    io_map_desc->io_map_size = io_map_size;
    io_map_desc->zone_size = zone_size;
    io_map_desc->io_map = (io_map_entry_t *)calloc(size, sizeof(io_map_entry_t));
    if (!io_map_desc->io_map)
        return 2;
    
    // TODO

    return 0;
}

int io_map_reset_zone(uint64_t zslba)
{
    if (!io_map_desc)
        return 1;
    
    if (!io_map_desc->io_map)
        return 2;

    memset(&io_map_desc->io_map[zslba], 0, io_map_desc->zone_size * sizeof(io_map_entry_t));

    return 0;
}