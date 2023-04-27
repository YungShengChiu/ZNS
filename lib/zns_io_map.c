#include "zns_io_map.h"

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
    
    if (io_map_desc->buffer_write_ptr)
        free(io_map_desc->buffer_write_ptr);

    if (io_map_desc->zns_write_ptr)
        free(io_map_desc->zns_write_ptr);

    if (io_map_desc->zone_state)
        free(io_map_desc->zone_state);

    free(io_map_desc);
    
    // TODO

    return 0;
}

int io_map_init(uint64_t io_map_size, uint64_t zone_size, uint64_t nr_zones)
{
    if (!io_map_desc)
        return 1;

    io_map_desc->io_map_size = io_map_size;
    io_map_desc->zone_size = zone_size;
    io_map_desc->nr_zones = nr_zones;
    io_map_desc->io_map = (io_map_entry_t *)calloc(io_map_size, sizeof(io_map_entry_t));
    if (!io_map_desc->io_map)
        return 2;
    
    io_map_desc->buffer_write_ptr = (uint64_t *)malloc(nr_zones * sizeof(uint64_t));
    if (!io_map_desc->buffer_write_ptr) {
        free(io_map_desc->io_map);
        return 2;
    }
    for (uint64_t i = 0; i < nr_zones; i++)
        io_map_desc->buffer_write_ptr[i] = i * zone_size;

    io_map_desc->zns_write_ptr = (uint64_t *)malloc(nr_zones * sizeof(uint64_t));
    if (!io_map_desc->zns_write_ptr) {
        free(io_map_desc->io_map);
        free(io_map_desc->buffer_write_ptr);
        return 2;
    }
    for (uint64_t i = 0; i < nr_zones; i++)
        io_map_desc->zns_write_ptr[i] = i * zone_size;

    io_map_desc->zone_state = (uint8_t *)calloc(nr_zones, sizeof(uint8_t));
    if (!io_map_desc->zone_state) {
        free(io_map_desc->io_map);
        free(io_map_desc->buffer_write_ptr);
        free(io_map_desc->zns_write_ptr);
        return 2;
    }

    // TODO

    return 0;
}

int io_map_reset_zone(uint64_t zslba, bool select_all)
{
    if (!io_map_desc)
        return 1;
    
    if (!io_map_desc->io_map)
        return 2;

    if (select_all) {
        memset(io_map_desc->io_map, 0, io_map_desc->io_map_size * sizeof(io_map_entry_t));
        memset(io_map_desc->zone_state, ZONE_STATE_EMPTY, io_map_desc->nr_zones * sizeof(uint8_t));
        for (uint64_t i = 0; i < io_map_desc->nr_zones; i++)
            io_map_desc->zns_write_ptr[i] = i * io_map_desc->zone_size;
    } else {
        uint32_t z_id = zslba / io_map_desc->zone_size;
        memset(&io_map_desc->io_map[zslba], 0, io_map_desc->zone_size * sizeof(io_map_entry_t));
        io_map_desc->zns_write_ptr[z_id] = zslba;
        io_map_desc->zone_state[z_id] = ZONE_STATE_EMPTY;
    }

    return 0;
}