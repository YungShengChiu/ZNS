#include "zns_zone_manage.h"

int zns_reset_zone_cb(zone_management_args_t *args)
{
    int rc = io_map_reset_zone(args->zslba, args->select_all);
    if (rc)
        return rc;
    
    rc = io_buffer_reset_zone(args->zslba, args->select_all);
    if (rc)
        return rc;
    
    return 0;
}

int zns_open_zone_cb(zone_management_args_t *args)
{
    io_buffer_entry_t *io_buffer_entry = io_buffer_q_find(args->zslba);
    if (!io_buffer_entry) {
        uint64_t available_blocks = 
                io_map_desc->zone_size - (io_map_get_z_wp(args->z_id) - args->zslba);
        
        int rc = io_buffer_init_q(&io_buffer_entry, args->zslba, available_blocks);
        if (rc)
            return rc;
        
        rc = io_buffer_enqueue(io_buffer_entry);
        if (rc)
        {
            return rc;
        }
    }

    return io_map_exp_open_zone(args->zslba, args->select_all);
}

int zns_close_zone_cb(zone_management_args_t *args)
{
    return io_map_close_zone(args->zslba, args->select_all);
}

int zns_finish_zone_cb(zone_management_args_t *args)
{
    return io_map_finish_zone(args->zslba, args->select_all);
}

int zns_offline_zone_cb(zone_management_args_t *args)
{
    return 0;
}

int zns_append_zone_cb(zone_io_args_t *args)
{
    return io_map_append_zns(args->z_id, args->lba, args->lba_count);
}

int zns_read_zone_cb(zone_io_args_t *args)
{
    return 0;
}