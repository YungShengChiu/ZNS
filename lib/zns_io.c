#include "zns_io.h"
#include "zns_internal.h"

io_buffer_desc_t *io_buffer_desc = NULL;
io_map_desc_t *io_map_desc = NULL;
zns_info_t *zns_info = NULL;
zns_io_lock_t *zns_io_lock = NULL;

static bool _probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, 
                        struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "\nAttaching to %s\n", trid->traddr);
    return true;
}

static void _attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, 
                        struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "\nAttached to %s\n", trid->traddr);

    if (!zns_info)
        return;
    
    if (!zns_info->spdk_struct)
        return;

    zns_info->spdk_struct->ctrlr = ctrlr;
}

static int _init_spdk(struct spdk_env_opts *opts, struct spdk_nvme_transport_id *trid, uint32_t nsid)
{
    if (!zns_info->spdk_struct)
        return 501;

    spdk_env_opts_init(opts);
    spdk_nvme_trid_populate_transport(trid, SPDK_NVME_TRANSPORT_PCIE);
    snprintf(trid->subnqn, sizeof(trid->subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);

    int rc = spdk_env_init(opts);
    if (rc < 0)
        return rc;

    rc = spdk_nvme_probe(trid, NULL, _probe_cb, _attach_cb, NULL);
    if (rc) {
        fprintf(stderr, "spdk_nvme_probe() failed\n");
        return rc;
    }

    if (!zns_info->spdk_struct->ctrlr)
        return 554;
    
    zns_info->spdk_struct->ns = spdk_nvme_ctrlr_get_ns(zns_info->spdk_struct->ctrlr, nsid);
    if (!spdk_nvme_ns_is_active(zns_info->spdk_struct->ns)) 
        return 555;
    
    zns_info->spdk_struct->qpair = spdk_nvme_ctrlr_alloc_io_qpair(zns_info->spdk_struct->ctrlr, NULL, 0);
    if (!zns_info->spdk_struct->qpair)
        return 556;

    zns_info->spdk_struct->opts = opts;
    zns_info->spdk_struct->trid = trid;

    return 0;
}

static int _init_zns_info(void)
{
    zns_info = (zns_info_t *)calloc(1, sizeof(zns_info_t));
    if (!zns_info)
        return 550;
    
    zns_info->spdk_struct = (spdk_struct_t *)calloc(1, sizeof(spdk_struct_t));
    if (!zns_info->spdk_struct) {
        free(zns_info);
        return 551;
    }

    return 0;
}

static int _init_zns_io_lock(void)
{
    zns_io_lock = (zns_io_lock_t *)calloc(1, sizeof(zns_io_lock_t));
    if (!zns_io_lock)
        return 650;
    
    pthread_mutex_init(&zns_io_lock->wb_lock, 0);
    pthread_mutex_init(&zns_io_lock->io_buffer_lock, 0);

    zns_io_lock->zone_lock = (pthread_mutex_t *)malloc(zns_info->nr_zones * sizeof(pthread_mutex_t));
    if (!zns_io_lock->zone_lock) {
        free(zns_io_lock);
        return 651;
    }

    for (uint64_t z_id = 0; z_id < zns_info->nr_zones; z_id++)
        pthread_mutex_init(&zns_io_lock->zone_lock[z_id], 0);
    
    return 0;
}

int zns_env_init(struct spdk_env_opts *opts, char *opts_name, struct spdk_nvme_transport_id *trid, uint32_t nsid)
{
    if (!opts || !trid || !opts_name)
        return 1000;
    
    opts->name = opts_name;
    
    int rc = _init_zns_info();
    if (rc)
        return rc;
    
    rc = _init_spdk(opts, trid, nsid);
    if (rc) {
        zns_env_fini();
        return rc;
    }
    
    zns_info->nr_zones = spdk_nvme_zns_ns_get_num_zones(zns_info->spdk_struct->ns);
    zns_info->nr_blocks_in_ns = spdk_nvme_ns_get_num_sectors(zns_info->spdk_struct->ns);
    zns_info->nr_blocks_in_zone = spdk_nvme_zns_ns_get_zone_size_sectors(zns_info->spdk_struct->ns);
    zns_info->block_size = spdk_nvme_ns_get_sector_size(zns_info->spdk_struct->ns);
    zns_info->zasl = spdk_nvme_zns_ctrlr_get_max_zone_append_size(zns_info->spdk_struct->ctrlr);
    
    for (size_t size = 1; size != zns_info->block_size; size <<= 1)
        zns_info->pow2_block_size++;
    
    io_buffer_desc = io_buffer_new();
    rc = io_buffer_init(spdk_nvme_zns_ns_get_max_open_zones(zns_info->spdk_struct->ns));
    if (rc) {
        zns_env_fini();
        return rc;
    }

    io_map_desc = io_map_new();
    rc = io_map_init(zns_info->nr_blocks_in_ns, zns_info->nr_blocks_in_zone, zns_info->nr_zones);
    if (rc) {
        zns_env_fini();
        return rc;
    }

    rc = buffer_pool_init(io_buffer_desc->buffer_pool_p, io_buffer_desc->q_max_nums, 
                        zns_info->nr_blocks_in_zone << zns_info->pow2_block_size, zns_info->block_size);
    if (rc) {
        zns_env_fini();
        return rc;
    }

    rc = _init_zns_io_lock();
    if (rc) {
        zns_env_fini();
        return rc;
    }

    return 0;
}

void zns_env_fini(void)
{
    zns_io_buf_lock();
    zns_wb_lock();
    for (uint64_t z_id = 0; z_id < zns_info->nr_zones; z_id++)
        zns_lock_zone(z_id);
    
    if (io_buffer_desc)
        buffer_pool_free(io_buffer_desc->buffer_pool_p);
    
    io_buffer_free();
    io_map_free();

    if (zns_io_lock) {
        pthread_mutex_destroy(&zns_io_lock->wb_lock);
        pthread_mutex_destroy(&zns_io_lock->io_buffer_lock);

        if (zns_io_lock->zone_lock)
            for (uint64_t z_id = 0; z_id < zns_info->nr_zones; z_id++)
                pthread_mutex_destroy(&zns_io_lock->zone_lock[z_id]);

        free(zns_io_lock->zone_lock);
        free(zns_io_lock);
    }

    if (!zns_info->spdk_struct)
        return;
    
    if (zns_info->spdk_struct->ctrlr) {
        struct spdk_nvme_detach_ctx *detach_ctx = NULL;
        spdk_nvme_detach_async(zns_info->spdk_struct->ctrlr, &detach_ctx);
        if (detach_ctx)
            spdk_nvme_detach_poll(detach_ctx);
    }

    free(zns_info->spdk_struct);
    free(zns_info);

    spdk_env_fini();
}

static void _zns_zone_io_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(zns_info->spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        //exit(1);
    }

    zone_io_args_t *args = (zone_io_args_t *)arg;
    rc = args->cb_fn(args);
    if (rc) {
        //  TODO: error handling
    }

    args->is_complete = true;
}

static int _wb_zone(io_buffer_entry_t *io_buffer_entry)
{
    if (!io_buffer_desc)
        return 150;
    
    if (!io_buffer_entry)
        return 151;
    
    uint64_t zslba = io_buffer_entry->q_desc_p->q_id;
    uint64_t z_id = zslba / zns_info->nr_blocks_in_zone;
    uint64_t lba;
    uint32_t lba_count;
    q_entry_t *q_entry = NULL;

    /**
     *      args should be release in the callback function
     */
    zone_io_args_t *args = (zone_io_args_t *)malloc(sizeof(zone_io_args_t));
    args->cb_fn = zns_append_zone_cb;
    args->zslba = zslba;
    args->z_id = z_id;
    
    int rc;
    for (uint64_t offset = 0; offset < zns_info->nr_blocks_in_zone; offset += lba_count) {
        lba = zslba + offset;
        if (io_map_lba_in_buffer(lba)) {
            q_entry = io_map_get_q_entry(lba);
            lba_count = q_entry->size;
            q_remove(q_entry);
            
            args->lba = lba;
            args->lba_count = lba_count;
            args->is_complete = false;
            rc = spdk_nvme_zns_zone_append(zns_info->spdk_struct->ns, zns_info->spdk_struct->qpair, 
                        q_entry->payload, zslba, lba_count, _zns_zone_io_cb, args, 0);
            if (rc) {
                //  TODO: error handling
                free(args);
                return rc;
            }
            for (; !args->is_complete; spdk_nvme_qpair_process_completions(zns_info->spdk_struct->qpair, 0));

            q_release_entry(q_entry);
        } else {
            lba_count = 1;
        }
    }

    rc = q_free(io_buffer_entry->q_desc_p);
    if (rc == 130)
        free(io_buffer_entry->q_desc_p);
    
    rc = buffer_pool_reset_entry(io_buffer_desc->buffer_pool_p, io_buffer_entry->buffer_entry_p);
    if (rc)
        return rc;

    free(io_buffer_entry);
    free(args);

    return 0;
}

static void _wb_zone_start(void *arg)
{
    io_buffer_entry_t *io_buffer_entry = (io_buffer_entry_t *)arg;
    int rc = _wb_zone(io_buffer_entry);
    switch (rc)
    {
        case 150:
        case 151:
        case 0:
            break;
    
        default:
            printf("wb rc = %d\n", rc);
            zns_io_buf_lock();
            io_buffer_insert_tail(io_buffer_entry);
            zns_io_buf_unlock();
            break;
    }

    zns_wb_unlock();
    pthread_exit(NULL);
}

static void _zns_zone_manage_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    zone_management_args_t *args = (zone_management_args_t *)arg;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(zns_info->spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        //zns_unlock_zone(args->z_id);
        //exit(1);
    }
    
    rc = args->cb_fn(args);
    if (rc) {
        //  TODO: error handling
    }
    
    zns_unlock_zone(args->z_id);
    args->is_complete = true;
    
}

int zns_reset_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= zns_info->nr_blocks_in_ns)
        return 1100;
    
    /**
     *      args should be release in the callback function
     */
    
    uint64_t z_id = zslba / zns_info->nr_blocks_in_zone;
    zone_management_args_t *args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
    args->cb_fn = zns_reset_zone_cb;
    args->zslba = zslba;
    args->z_id = z_id;
    args->select_all = select_all;
    args->is_complete = false;
    
    /**
     *      The lock should be unlock in the callback function
     */
    
    zns_lock_zone(z_id);
    
    int rc = spdk_nvme_zns_reset_zone(zns_info->spdk_struct->ns, zns_info->spdk_struct->qpair, 
                        zslba, select_all, _zns_zone_manage_cb, args);
    if (rc) {
        zns_unlock_zone(z_id);
        free(args);
        return rc;
    }
    for (; !args->is_complete; spdk_nvme_qpair_process_completions(zns_info->spdk_struct->qpair, 0));

    free(args);

    return 0;
}

int zns_open_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= zns_info->nr_blocks_in_ns)
        return 1100;
    
    zone_management_args_t *args = NULL;
    switch (io_map_state(zslba)) {
        case ZONE_STATE_EXP_OPEN:
            return 1201;

        case ZONE_STATE_EMPTY:
        case ZONE_STATE_CLOSED:
            int rc;
            for (; io_buffer_desc->q_nums >= io_buffer_desc->q_max_nums;) {
                rc = zns_close_zone(io_buffer_q_last()->q_desc_p->q_id, false);
                if (rc) {
                    //  TODO: error handling
                    return rc;
                }
            }

        case ZONE_STATE_IMP_OPEN:
            /**
             *      args should be release in the callback function
             */

            uint64_t z_id = zslba / zns_info->nr_blocks_in_zone;
            args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
            args->cb_fn = zns_open_zone_cb;
            args->zslba = zslba;
            args->z_id = z_id;
            args->select_all = select_all;
            args->is_complete = false;

            /**
             *      The lock should be unlock in the callback function
             */
            
            zns_lock_zone(z_id);

            rc = spdk_nvme_zns_open_zone(zns_info->spdk_struct->ns, zns_info->spdk_struct->qpair, 
                        zslba, select_all, _zns_zone_manage_cb, args);
            if (rc) {
                zns_unlock_zone(z_id);
                free(args);
                return rc;
            }
            for (; !args->is_complete; spdk_nvme_qpair_process_completions(zns_info->spdk_struct->qpair, 0));
            break;

        case ZONE_STATE_FULL:
            return 1202;

        case ZONE_STATE_READONLY:
            return 1203;
        
        case ZONE_STATE_OFFLINE:
            return 1204;
        
        default:
            return 1200;
    }

    free(args);

    return 0;
}

int zns_close_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= zns_info->nr_blocks_in_ns)
        return 1100;

    uint64_t z_id = zslba / zns_info->nr_blocks_in_zone;
    zone_management_args_t *args = NULL;
    switch (io_map_state(zslba)) {       
        case ZONE_STATE_EMPTY:
            return 1210;
        
        case ZONE_STATE_IMP_OPEN:
        case ZONE_STATE_EXP_OPEN:
            io_buffer_entry_t *io_buffer_entry = io_buffer_q_find(zslba);
            if (!io_buffer_entry)
                return 175;
            
            /**
             *      The lock should be unlock in the callback function
             */

            zns_lock_zone(z_id);
            
            zns_io_buf_lock();
            io_buffer_remove(io_buffer_entry);
            zns_io_buf_unlock();
            
            zns_wb_lock();

            int rc = _wb_zone(io_buffer_entry);
            switch (rc)
            {
                case 150:
                case 151:
                case 0:
                    break;
    
                default:
                    fprintf(stderr, "wb rc = %d\n", rc);
                    zns_io_buf_lock();
                    io_buffer_insert_tail(io_buffer_entry);
                    zns_io_buf_unlock();
                    break;
            }

            zns_wb_unlock();

            /**
             *      args should be release in the callback function
             */

            args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
            args->cb_fn = zns_close_zone_cb;
            args->zslba = zslba;
            args->z_id = z_id;
            args->select_all = select_all;
            args->is_complete = false;

            rc = spdk_nvme_zns_close_zone(zns_info->spdk_struct->ns, zns_info->spdk_struct->qpair, 
                        zslba, select_all, _zns_zone_manage_cb, args);
            if (rc) {
                zns_unlock_zone(z_id);
                free(args);
                return rc;
            }
            for (; !args->is_complete; spdk_nvme_qpair_process_completions(zns_info->spdk_struct->qpair, 0));
            
            break;

        case ZONE_STATE_CLOSED:
            return 1211;
        
        case ZONE_STATE_FULL:
            return 1212;

        case ZONE_STATE_READONLY:
            return 1213;

        case ZONE_STATE_OFFLINE:
            return 1214;
        
        default:
            return 1200;
    }
    
    free(args);

    return 0;
}

int zns_finish_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= zns_info->nr_blocks_in_ns)
        return 1100;
    
    uint64_t z_id = zslba / zns_info->nr_blocks_in_zone;
    zone_management_args_t *args = NULL;
    int rc;
    switch (io_map_state(zslba)) {
        case ZONE_STATE_EMPTY:
            return 1220;
        
        case ZONE_STATE_IMP_OPEN:
        case ZONE_STATE_EXP_OPEN:
            io_buffer_entry_t *io_buffer_entry = io_buffer_q_find(zslba);
            if (!io_buffer_entry)
                return 175;
            
            /**
             *      The lock should be unlock in the callback function
             */

            zns_lock_zone(z_id);
            zns_io_buf_lock();
            io_buffer_remove(io_buffer_entry);
            zns_io_buf_unlock();

            zns_wb_lock();

            rc = _wb_zone(io_buffer_entry);
            switch (rc)
            {
                case 150:
                case 151:
                case 0:
                    break;
    
                default:
                    fprintf(stderr, "wb rc = %d\n", rc);
                    zns_io_buf_lock();
                    io_buffer_insert_tail(io_buffer_entry);
                    zns_io_buf_unlock();
                    break;
            }

            zns_wb_unlock();

            /**
             *      args should be release in the callback function
             */

            args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
            args->cb_fn = zns_finish_zone_cb;
            args->zslba = zslba;
            args->z_id = z_id;
            args->select_all = select_all;
            args->is_complete = false;

            rc = spdk_nvme_zns_finish_zone(zns_info->spdk_struct->ns, zns_info->spdk_struct->qpair, 
                        zslba, select_all, _zns_zone_manage_cb, args);
            if (rc) {
                zns_unlock_zone(z_id);
                free(args);
                return rc;
            }
            for (; !args->is_complete; spdk_nvme_qpair_process_completions(zns_info->spdk_struct->qpair, 0));

            break;
        case ZONE_STATE_CLOSED:
            /**
             *      args should be release in the callback function
             */

            args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
            args->cb_fn = zns_finish_zone_cb;
            args->zslba = zslba;
            args->z_id = z_id;
            args->select_all = select_all;
            args->is_complete = false;

            /**
             *      The lock should be unlock in the callback function
             */

            zns_lock_zone(z_id);

            rc = spdk_nvme_zns_finish_zone(zns_info->spdk_struct->ns, zns_info->spdk_struct->qpair, 
                        zslba, select_all, _zns_zone_manage_cb, args);
            if (rc) {
                zns_unlock_zone(z_id);
                free(args);
                return rc;
            }
            for (; !args->is_complete; spdk_nvme_qpair_process_completions(zns_info->spdk_struct->qpair, 0));

            break;
        
        case ZONE_STATE_FULL:
            return 1221;
        
        case ZONE_STATE_READONLY:
            return 1222;
        
        case ZONE_STATE_OFFLINE:
            return 1223;
        
        default:
            return 1200;
    }

    free(args);

    return 0;
}

/**
 *      TODO Offline zone is not supported yet
 */

int zns_offline_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= zns_info->nr_blocks_in_ns)
        return 1100;
    
    switch (io_map_state(zslba)) {
        case ZONE_STATE_EMPTY:
        case ZONE_STATE_IMP_OPEN:
        case ZONE_STATE_EXP_OPEN:
        case ZONE_STATE_CLOSED:
        case ZONE_STATE_FULL:
        case ZONE_STATE_READONLY:
        case ZONE_STATE_OFFLINE:
        default:
            return 1200;
    }

    return 0;
}

static void io_buffer_upsert(io_buffer_entry_t *io_buffer_entry)
{
    zns_io_buf_lock();
    io_buffer_remove(io_buffer_entry);
    io_buffer_insert_front(io_buffer_entry);
    zns_io_buf_unlock();
}

static void io_buffer_q_upsert(q_entry_t *q_entry)
{
    q_remove(q_entry);
    q_insert_front(q_entry);
}

int zns_io_append(void *payload, uint64_t zslba, uint32_t lba_count)
{
    if (zslba >= zns_info->nr_blocks_in_ns)
        return 1100;
    
    if (!payload)
        return 1000;
    
    if (lba_count > zns_info->nr_blocks_in_zone)
        return 1101;
    
    uint64_t z_id = zslba / zns_info->nr_blocks_in_zone;
    zns_lock_zone(z_id);

    uint64_t wp = io_map_get_buf_wp(z_id);
    io_buffer_entry_t *io_buffer_entry = NULL;
    q_entry_t *q_entry = NULL;
    int rc;
    switch (io_map_state(zslba)) {
        default:
            return 1200;
        
        case ZONE_STATE_EMPTY:
            rc = io_map_imp_open_zone(zslba);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }

            rc = io_buffer_init_q(&io_buffer_entry, zslba, zns_info->nr_blocks_in_zone);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }
            
            for (; io_buffer_desc->q_nums >= io_buffer_desc->q_max_nums;) {
                rc = zns_close_zone(io_buffer_q_last()->q_desc_p->q_id, false);
                if (rc) {
                    //  TODO: error handling
                    zns_unlock_zone(z_id);
                    return rc;
                }
            }
            
            rc = q_enqueue(io_buffer_entry->q_desc_p, &q_entry, payload, lba_count);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }
            
            rc = io_buffer_enqueue(io_buffer_entry);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }
            
            rc = io_map_append_buf(wp, q_entry, lba_count);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }

            break;
        case ZONE_STATE_IMP_OPEN:
        case ZONE_STATE_EXP_OPEN:
            io_buffer_entry = io_buffer_q_find(zslba);
            if (!io_buffer_entry) {
                //  TODO: error handling
                //rc = io_buffer_init_q(&io_buffer_entry, zslba, zns_info->nr_blocks_in_zone);
                zns_unlock_zone(z_id);
                return 175;
            }

            if (io_buffer_entry->q_desc_p->q_size + lba_count > io_buffer_entry->q_desc_p->q_size_max) {
                //  TODO: error handling
                //  Print the message
                zns_unlock_zone(z_id);
                return 1102;
            }

            rc = q_enqueue(io_buffer_entry->q_desc_p, &q_entry, payload, lba_count);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }

            rc = io_map_append_buf(wp, q_entry, lba_count);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }

            io_buffer_upsert(io_buffer_entry);

            break;
        case ZONE_STATE_CLOSED:
            rc = io_map_imp_open_zone(zslba);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }

            uint64_t available_blocks = 
                    zns_info->nr_blocks_in_zone - (io_map_get_z_wp(z_id) - zslba);
            
            rc = io_buffer_init_q(&io_buffer_entry, zslba, available_blocks);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }

            for (; io_buffer_desc->q_nums >= io_buffer_desc->q_max_nums;) {
                rc = zns_close_zone(io_buffer_q_last()->q_desc_p->q_id, false);
                if (rc) {
                    //  TODO: error handling
                    zns_unlock_zone(z_id);
                    return rc;
                }
            }

            rc = q_enqueue(io_buffer_entry->q_desc_p, &q_entry, payload, lba_count);
            if (rc) {
                //  TODO: error handling.
                zns_unlock_zone(z_id);
                return rc;
            }

            rc = io_buffer_enqueue(io_buffer_entry);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }

            rc = io_map_append_buf(wp, q_entry, lba_count);
            if (rc) {
                //  TODO: error handling
                zns_unlock_zone(z_id);
                return rc;
            }

            break;
        case ZONE_STATE_FULL:
            zns_unlock_zone(z_id);
            return 1150;

        case ZONE_STATE_READONLY:
            zns_unlock_zone(z_id);
            return 1151;
            
        case ZONE_STATE_OFFLINE:
            zns_unlock_zone(z_id);
            return 1152;
    }
    
    zns_unlock_zone(z_id);

    return 0;
}

int zns_io_update(uint64_t lba, zns_io_update_cb cb_fn, void *cb_arg)
{
    if (lba >= zns_info->nr_blocks_in_ns)
        return 1100;
    
    uint64_t z_id = lba / zns_info->nr_blocks_in_zone;
    zns_lock_zone(z_id);

    q_entry_t *q_entry;
    switch (io_map_get_identifier(lba))
    {
        default:
            zns_unlock_zone(z_id);
            return 1110;
        
        case 0x0:
            /* The data is not in ZNS nor io_buffer */
            zns_unlock_zone(z_id);
            return 301;
        
        case 0x1:
        case 0x3:
            /* The data is in io_buffer */
            q_entry = io_map_get_q_entry(lba);
            cb_fn(cb_arg, q_entry->payload, q_entry->size);

            io_buffer_q_upsert(q_entry);
            io_buffer_upsert(q_entry->q_desc_p->io_buffer_entry_p);

            break;
    }

    zns_unlock_zone(z_id);

    return 0;
}

/**
 *      payload is a parameter that points to a pointer to the data buffer.
 */

int zns_io_read(void **payload, uint64_t lba, uint32_t lba_count)
{
    if (lba >= zns_info->nr_blocks_in_ns)
        return 1100;
    
    if(!payload)
        return 1000;
    
    uint64_t z_id = lba / zns_info->nr_blocks_in_zone;
    zns_lock_zone(z_id);
    
    size_t data_size = lba_count << zns_info->pow2_block_size;
    q_entry_t *q_entry;
    int rc;
    switch (io_map_get_identifier(lba))
    {
        default:
            zns_unlock_zone(z_id);
            return 300;
        
        case 0x0:
            /* The data is not in ZNS nor io_buffer */
            zns_unlock_zone(z_id);
            return 301;
        
        case 0x1:
            /* The data is in io_buffer */
            q_entry = io_map_get_q_entry(lba);
            *payload = spdk_dma_malloc(data_size, zns_info->block_size, NULL);
            memcpy(*payload, q_entry->payload, data_size);

            io_buffer_q_upsert(q_entry);
            io_buffer_upsert(q_entry->q_desc_p->io_buffer_entry_p);
            
            break;
        
        case 0x2:
            /* The data is in ZNS */
            /* The data won't be buffered */

            /**
             *      args should be release in the callback function
             */
            zone_io_args_t *args = (zone_io_args_t *)malloc(sizeof(zone_io_args_t));
            args->cb_fn = zns_read_zone_cb;
            args->z_id = z_id;
            args->lba = lba;
            args->lba_count = lba_count;
            args->is_complete = false;

            *payload = spdk_dma_malloc(data_size, zns_info->block_size, NULL);
            rc = spdk_nvme_ns_cmd_read(zns_info->spdk_struct->ns, zns_info->spdk_struct->qpair, 
                        *payload, io_map_get_lba(lba), lba_count, _zns_zone_io_cb, args, 0);
            if (rc) {
                zns_unlock_zone(z_id);
                free(args);
                return rc;
            }
            for (; !args->is_complete; spdk_nvme_qpair_process_completions(zns_info->spdk_struct->qpair, 0));

            break;
        
        case 0x3:
            /* The data is in ZNS and io_buffer */
            q_entry = io_map_get_q_entry(lba);
            *payload = spdk_dma_malloc(data_size, zns_info->block_size, NULL);
            memcpy(*payload, q_entry->payload, data_size);
            io_buffer_q_upsert(q_entry);
            io_buffer_upsert(q_entry->q_desc_p->io_buffer_entry_p);
            
            break;
        
        case 0x11:
            //  TODO Read the data from the io_buffer of the starting lba
    }

    zns_unlock_zone(z_id);

    return 0;
}

void *zns_io_malloc(size_t size, uint64_t zslba)
{
    if (zslba >= zns_info->nr_blocks_in_ns)
        return NULL;
    
    if (size > (zns_info->nr_blocks_in_zone << zns_info->pow2_block_size))
        return NULL;
    io_buffer_entry_t *io_buffer_entry  = io_buffer_q_find(zslba);
    if (!io_buffer_entry)
        return spdk_dma_malloc(size, zns_info->block_size, NULL);
    void *data = io_buffer_entry->buffer_entry_p->sp;
    io_buffer_entry->buffer_entry_p->sp += size;

    return data;
}

const spdk_struct_t *zns_get_spdk_struct(void)
{
    return (const spdk_struct_t *)zns_info->spdk_struct;
}

uint64_t zns_get_nr_zones(void)
{
    return zns_info->nr_zones;
}

uint64_t zns_get_nr_blocks_in_ns(void)
{
    return zns_info->nr_blocks_in_ns;
}

uint64_t zns_get_nr_blocks_in_zone(void)
{
    return zns_info->nr_blocks_in_zone;
}

size_t zns_get_block_size(void)
{
    return zns_info->block_size;
}

uint32_t zns_get_zone_append_size_limit(void)
{
    return zns_info->zasl;
}