#include "zns_io_map.h"

io_map_desc_t *io_map_new(void)
{
    io_map_desc_t *desc = (io_map_desc_t *)calloc(1, sizeof(io_map_desc_t));
    return desc;
}

int io_map_init(uint64_t io_map_size, uint64_t zone_size, uint64_t nr_zones)
{
    if (!io_map_desc)
        return 200;
    
    io_map_desc->io_map_size = io_map_size;
    io_map_desc->zone_size = zone_size;
    io_map_desc->nr_zones = nr_zones;

    io_map_desc->io_map = (io_map_entry_t *)calloc(io_map_size, sizeof(io_map_entry_t));
    if (!io_map_desc->io_map)
        return 251;
    
    io_map_desc->buf_wp = (uint64_t *)malloc(nr_zones * sizeof(uint64_t));
    if (!io_map_desc->buf_wp) {
        free(io_map_desc->io_map);
        return 252;
    }
    for (uint64_t i = 0; i < nr_zones; i++)
        io_map_desc->buf_wp[i] = i * zone_size;
    
    io_map_desc->z_wp = (uint64_t *)malloc(nr_zones * sizeof(uint64_t));
    if (!io_map_desc->z_wp) {
        free(io_map_desc->io_map);
        free(io_map_desc->buf_wp);
        return 253;
    }
    for (uint64_t i = 0; i < nr_zones; i++)
        io_map_desc->z_wp[i] = i * zone_size;

    io_map_desc->z_state = (uint8_t *)calloc(nr_zones, sizeof(uint8_t));
    if (!io_map_desc->z_state) {
        free(io_map_desc->io_map);
        free(io_map_desc->buf_wp);
        free(io_map_desc->z_wp);
        return 254;
    }

    return 0;
}

int io_map_free(void)
{
    if (!io_map_desc)
        return 200;
    
    if (io_map_desc->io_map)
        free(io_map_desc->io_map);
    
    if (io_map_desc->buf_wp)
        free(io_map_desc->buf_wp);

    if (io_map_desc->z_wp)
        free(io_map_desc->z_wp);

    if (io_map_desc->z_state)
        free(io_map_desc->z_state);

    free(io_map_desc);

    return 0;
}

int io_map_reset_zone(uint64_t zslba, bool select_all)
{
    if (!io_map_desc)
        return 200;
    
    if (!io_map_desc->io_map)
        return 201;
    
    if (!io_map_desc->buf_wp)
        return 202;
    
    if (!io_map_desc->z_wp)
        return 203;
    
    if (!io_map_desc->z_state)
        return 204;
    
    if (select_all) {
        memset(io_map_desc->io_map, 0, io_map_desc->io_map_size * sizeof(io_map_entry_t));
        memset(io_map_desc->z_state, ZONE_STATE_EMPTY, io_map_desc->nr_zones * sizeof(uint8_t));
        uint64_t wp;
        for (uint64_t i = 0; i < io_map_desc->nr_zones; i++) {
            wp = i * io_map_desc->zone_size;
            io_map_desc->buf_wp[i] = wp;
            io_map_desc->z_wp[i] = wp;
        }
    } else {
        uint64_t z_id = zslba / io_map_desc->zone_size;
        memset(&io_map_desc->io_map[zslba], 0, io_map_desc->zone_size * sizeof(io_map_entry_t));
        io_map_desc->z_state[z_id] = ZONE_STATE_EMPTY;
        io_map_desc->buf_wp[z_id] = zslba;
        io_map_desc->z_wp[z_id] = zslba;
    }

    return 0;
}

int io_map_imp_open_zone(uint64_t zslba)
{
    uint64_t z_id = zslba / io_map_desc->zone_size;
    io_map_desc->z_state[z_id] = ZONE_STATE_IMP_OPEN;
    
    return 0;
}

int io_map_exp_open_zone(uint64_t zslba, bool select_all)
{
    if (select_all) {
        // TODO

    } else {
        uint64_t z_id = zslba / io_map_desc->zone_size;
        io_map_desc->z_state[z_id] = ZONE_STATE_EXP_OPEN;
    }
    
    return 0;
}

int io_map_close_zone(uint64_t zslba, bool select_all)
{
    if (select_all) {
        // TODO

    } else {
        uint64_t z_id = zslba / io_map_desc->zone_size;
        io_map_desc->z_state[z_id] = ZONE_STATE_CLOSED;
    }
    
    return 0;
}

int io_map_finish_zone(uint64_t zslba, bool select_all)
{
    if (select_all) {
        // TODO
        
    } else {
        uint64_t z_id = zslba / io_map_desc->zone_size;
        io_map_desc->z_state[z_id] = ZONE_STATE_FULL;
        io_map_desc->z_wp[z_id] = zslba + io_map_desc->zone_size;
    }
    
    return 0;
}

int io_map_offline_zone(uint64_t zslba, bool select_all)
{
    if (select_all) {
        // TODO
        
    } else {
        uint64_t z_id = zslba / io_map_desc->zone_size;
        io_map_desc->z_state[z_id] = ZONE_STATE_OFFLINE;
    }
    
    return 0;
}

int io_map_append_buf(uint64_t wp, q_entry_t *q_entry, uint64_t lba_count)
{
    if (!q_entry)
        return 101;
    
    io_map_desc->io_map[wp].identifier = 0x1;
    io_map_desc->io_map[wp].q_entry = q_entry;
    io_map_desc->buf_wp[wp / io_map_desc->zone_size] += lba_count;

    return 0;
}

int io_map_append_zns(uint64_t z_id, uint64_t wp, uint64_t lba_count)
{
    io_map_entry_t *io_map_entry;
    for (uint64_t offset = 0; offset < lba_count; offset++) {
        io_map_entry = &io_map_desc->io_map[wp + offset];
        io_map_entry->lba = io_map_desc->z_wp[z_id] + offset;
        io_map_entry->identifier = 0x2;
    }

    io_map_desc->z_wp[z_id] += lba_count;
    io_map_desc->io_map[wp].q_entry = NULL;

    return 0;
}