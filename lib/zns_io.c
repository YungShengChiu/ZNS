#include "zns_io.h"
#include "zns_io_internal.h"

static uint64_t nr_blocks_in_zone;
static uint64_t nr_blocks_in_ns;
static size_t block_size;
static uint64_t nr_zones;
static uint32_t zasl;
static uint8_t pow2_block_size;
static spdk_struct_t *spdk_struct;

io_buffer_desc_t *io_buffer_desc = NULL;
io_map_desc_t *io_map_desc = NULL;

static bool probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "\nAttaching to %s\n", trid->traddr);
    return true;
}

static void attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr *ctrlr, struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "Attached to %s\n", trid->traddr);
    *(struct spdk_nvme_ctrlr **)cb_ctx = ctrlr;
}

static void zns_write_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    if (arg)
        *(bool *)arg = true;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        //exit(1);
    }
}

static void zns_read_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    if (arg)
        *(bool *)arg = true;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        //exit(1);
    }
}

static void zns_reset_zone_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{   
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
        //exit(1);
    }

    zone_management_args_t *args = (zone_management_args_t *)arg;
    io_map_reset_zone(args->zslba, args->select_all);
    io_buffer_reset_zone(args->zslba / nr_blocks_in_zone, args->select_all);
    args->is_complete = true;
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
    free(args);
}

static void zns_open_zone_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
        //exit(1);
    }

    zone_management_args_t *args = (zone_management_args_t *)arg;
    io_buffer_entry_t *io_buffer_entry = NULL;
    uint64_t z_id = args->zslba / nr_blocks_in_zone;
    
    rc = io_buffer_q_find(&io_buffer_entry, z_id);
    if (!rc) {
        uint64_t available_blocks = nr_blocks_in_zone - (io_map_desc->zns_write_ptr[z_id] - args->zslba);
        rc = io_buffer_q_init(&io_buffer_entry, z_id, available_blocks);
        if (rc) {
            //  TODO: Handle error
        }

        rc = io_buffer_enqueue(io_buffer_entry);
        if (rc) {
            //  TODO: Handle error
        }
    }

    io_map_desc->zone_state[z_id] = ZONE_STATE_EXP_OPEN;
    args->is_complete = true;
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
    free(args);
}

static void zns_close_zone_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
        //exit(1);
    }

    zone_management_args_t *args = (zone_management_args_t *)arg;
    io_map_desc->zone_state[args->zslba / nr_blocks_in_zone] = ZONE_STATE_CLOSED;
    args->is_complete = true;
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
    free(args);
}

static void zns_finish_zone_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
        //exit(1);
    }

    zone_management_args_t *args = (zone_management_args_t *)arg;
    uint64_t z_id = args->zslba / nr_blocks_in_zone;
    io_map_desc->zone_state[z_id] = ZONE_STATE_FULL;
    io_map_desc->zns_write_ptr[z_id] = args->zslba + nr_blocks_in_zone;
    args->is_complete = true;
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
    free(args);
}

static void zns_offline_zone_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
        //exit(1);
    }

    zone_management_args_t *args = (zone_management_args_t *)arg;
    io_map_desc->zone_state[args->zslba / nr_blocks_in_zone] = ZONE_STATE_OFFLINE;
    args->is_complete = true;
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
    free(args);
}

/**
 *      The function is a critical section
 */

static void io_buffer_upsert(io_buffer_entry_t *io_buffer_entry)
{
    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    io_buffer_remove(io_buffer_entry);
    io_buffer_insert_front(io_buffer_entry);
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
}

static void io_buffer_q_upsert(q_entry_t *q_entry)
{
    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    io_buffer_q_remove(q_entry);
    io_buffer_q_insert_front(q_entry);
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
}

static void io_buffer_release(void)
{
    int rc = io_buffer_free();
    if (rc)
        fprintf(stderr, "Failed to release I/O buffer!\n rc = %d\n", rc);
}

static void io_map_release(void)
{
    int rc = io_map_free();
    if (rc)
        fprintf(stderr, "Failed to release I/O map!\n rc = %d\n", rc);
}

int zns_env_init(struct spdk_env_opts *opts, char *opts_name, struct spdk_nvme_transport_id *trid, uint32_t nsid)
{
    spdk_struct = (spdk_struct_t *)calloc(1, sizeof(spdk_struct_t));
    
    spdk_env_opts_init(opts);
    opts->name = opts_name;
    spdk_struct->opts = opts;

    spdk_nvme_trid_populate_transport(trid, SPDK_NVME_TRANSPORT_PCIE);
    snprintf(trid->subnqn, sizeof(trid->subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);
    spdk_struct->trid = trid;

    int rc = spdk_env_init(opts);
    if (rc < 0) {
        zns_env_fini();
        return rc;
    }

    rc = spdk_nvme_probe(trid, &spdk_struct->ctrlr, probe_cb, attach_cb, NULL);
    if (rc) {
        fprintf(stderr, "spdk_nvme_probe() failed!\n");
        zns_env_fini();
        return rc;
    }

    if (!spdk_struct->ctrlr) {
        fprintf(stderr, "No NVMe controllers found!\n");
        zns_env_fini();
        return 1;
    }

    spdk_struct->ns = spdk_nvme_ctrlr_get_ns(spdk_struct->ctrlr, nsid);
    if (!spdk_nvme_ns_is_active(spdk_struct->ns)) {
        fprintf(stderr, "Namespace %d is inactive!\n", nsid);
        zns_env_fini();
        return rc;
    }

    spdk_struct->qpair = spdk_nvme_ctrlr_alloc_io_qpair(spdk_struct->ctrlr, NULL, 0);
    if (!spdk_struct->qpair) {
        fprintf(stderr, "NVMe I/O qpair allocation failed!\n");
        zns_env_fini();
        return rc;
    }

    io_buffer_desc = io_buffer_new();
    if (!io_buffer_desc) {
        fprintf(stderr, "Allocate io_buffer_desc failed!\n");
        zns_env_fini();
        return rc;
    }
    
    pthread_mutex_init(&io_buffer_desc->io_buffer_mutex, 0);

    io_buffer_desc->q_max_nums = spdk_nvme_zns_ns_get_max_open_zones(spdk_struct->ns);

    block_size = spdk_nvme_ns_get_sector_size(spdk_struct->ns);
    nr_zones = spdk_nvme_zns_ns_get_num_zones(spdk_struct->ns);
    nr_blocks_in_zone = spdk_nvme_zns_ns_get_zone_size_sectors(spdk_struct->ns);
    nr_blocks_in_ns = spdk_nvme_ns_get_num_sectors(spdk_struct->ns);
    zasl = spdk_nvme_zns_ctrlr_get_max_zone_append_size(spdk_struct->ctrlr);

    pow2_block_size = 0;
    for (size_t i = 1; i != block_size; i <<= 1)
        pow2_block_size++;

    io_map_desc = io_map_new();
    if (!io_map_desc) {
        fprintf(stderr, "Allocate io_map_desc failed!\n");
        zns_env_fini();
        return rc;
    }
    
    rc = io_map_init(nr_blocks_in_ns, nr_blocks_in_zone, nr_zones);
    if (rc) {
        fprintf(stderr, "io_map_init() failed!\n");
        zns_env_fini();
        return rc;
    }

    return rc;
}

void zns_env_fini(void)
{
    io_buffer_release();
    io_map_release();

    if (spdk_struct->ctrlr) {
        struct spdk_nvme_detach_ctx *detach_ctx = NULL;
        spdk_nvme_detach_async(spdk_struct->ctrlr, &detach_ctx);
        if (detach_ctx)
            spdk_nvme_detach_poll(detach_ctx);
    }

    spdk_env_fini();
}

static int io_buffer_wb_zone(io_buffer_entry_t *io_buffer_entry)
{
    printf("write back to zone\n");
    if (!io_buffer_desc || !io_map_desc)
        return 1;
    
    if (!io_buffer_entry)
        return 2;
    
    uint64_t zslba = io_buffer_entry->q_desc_p->q_id * nr_blocks_in_zone;
    uint64_t z_id = io_buffer_entry->q_desc_p->q_id;
    uint64_t lba;
    uint64_t lba_count;
    int rc;
    bool is_complete;
    q_entry_t *q_entry = NULL;
    io_buffer_q_desc_t *q_desc = io_buffer_entry->q_desc_p;
    for (uint64_t i = 0; i < nr_blocks_in_zone; i += lba_count) {
        lba = zslba + i;
        if (io_map_desc->io_map[lba].identifier == 1) {
            q_entry = io_map_desc->io_map[lba].q_entry;
            lba_count = q_entry->size;
            io_buffer_q_remove(q_entry);
            is_complete = false;
            rc = spdk_nvme_zns_zone_append(spdk_struct->ns, spdk_struct->qpair, q_entry->payload, zslba, lba_count, zns_write_cb, &is_complete, 0);
            if (rc) {
                //  TODO: handle error
            }
            for (; !is_complete; spdk_nvme_qpair_process_completions(spdk_struct->qpair, 0));
            spdk_free(io_buffer_q_release_entry(q_entry));
            for (uint64_t j = 0; j < lba_count; j++)
                io_map_desc->io_map[lba + j].lba = io_map_desc->zns_write_ptr[z_id] + j;
            io_map_desc->zns_write_ptr[z_id] += lba_count;
            io_map_desc->io_map[lba].q_entry = NULL;
            io_map_desc->io_map[lba].identifier = 2;
        } else {
            lba_count = 1;
        }
    }

    rc = io_buffer_q_free(q_desc);
    if (rc) {
        //  TODO: handle error
    }
    free(io_buffer_entry);
    
    return 0;
}

int zns_reset_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", zslba);
        return 2;
    }

    zone_management_args_t *args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
    args->zslba = zslba;
    args->select_all = select_all;
    args->is_complete = false;

    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    int rc = spdk_nvme_zns_reset_zone(spdk_struct->ns, spdk_struct->qpair, zslba, select_all, zns_reset_zone_cb, args);
    if (rc) {
        fprintf(stderr, "Reset zone failed!\n");
        free(args);
        return rc;
    }
    for (; !args->is_complete; spdk_nvme_qpair_process_completions(spdk_struct->qpair, 0));
    
    return 0;
}

int zns_open_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", zslba);
        return 2;
    }

    int rc;
    if (io_buffer_desc->q_nums >= io_buffer_desc->q_max_nums) {
        io_buffer_entry_t *io_buffer_entry;
        
        rc = io_buffer_dequeue(&io_buffer_entry);
        if (rc) {
            //  TODO: handle error
        }

        uint64_t wb_zslba = io_buffer_entry->q_desc_p->q_id * nr_blocks_in_zone;

        rc = io_buffer_wb_zone(io_buffer_entry);
        if (rc == 1) {
            //  TODO: handle error
        } else if (rc == 2) {
            //  TODO: handle error
        }

        rc = zns_close_zone(wb_zslba, false);
        if (rc) {
            //  TODO: handle error
        }

    }

    zone_management_args_t *args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
    args->zslba = zslba;
    args->select_all = select_all;
    args->is_complete = false;

    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    rc = spdk_nvme_zns_open_zone(spdk_struct->ns, spdk_struct->qpair, zslba, select_all, zns_open_zone_cb, args);
    if (rc) {
        fprintf(stderr, "Open zone failed!\n");
        free(args);
        return rc;
    }
    for (; !args->is_complete; spdk_nvme_qpair_process_completions(spdk_struct->qpair, 0));
    
    return 0;
}

/**
 *      select_all are not support yet in the following three functions
 */

int zns_close_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", zslba);
        return 2;
    }

    uint64_t z_id = zslba / nr_blocks_in_zone;
    io_buffer_entry_t *io_buffer_entry;
    int rc = io_buffer_q_find(&io_buffer_entry, z_id);
    if (rc) {
        io_buffer_remove(io_buffer_entry);
        rc = io_buffer_wb_zone(io_buffer_entry);
        if (rc == 1) {
            //  TODO: handle error
        } else if (rc == 2) {
            //  TODO: handle error
        }
    }

    zone_management_args_t *args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
    args->zslba = zslba;
    args->select_all = select_all;
    args->is_complete = false;

    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    rc = spdk_nvme_zns_close_zone(spdk_struct->ns, spdk_struct->qpair, zslba, select_all, zns_close_zone_cb, args);
    if (rc) {
        fprintf(stderr, "Close zone failed!\n");
        return rc;
    }
    for (; !args->is_complete; spdk_nvme_qpair_process_completions(spdk_struct->qpair, 0));
    
    return 0;
}

int zns_finish_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", zslba);
        return 2;
    }

    zone_management_args_t *args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
    args->zslba = zslba;
    args->select_all = select_all;
    args->is_complete = false;

    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    int rc = spdk_nvme_zns_finish_zone(spdk_struct->ns, spdk_struct->qpair, zslba, select_all, zns_finish_zone_cb, args);
    if (rc) {
        fprintf(stderr, "Finish zone failed!\n");
        return rc;
    }
    for (; !args->is_complete; spdk_nvme_qpair_process_completions(spdk_struct->qpair, 0));
    
    return 0;
}

int zns_offline_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", zslba);
        return 2;
    }

    zone_management_args_t *args = (zone_management_args_t *)malloc(sizeof(zone_management_args_t));
    args->zslba = zslba;
    args->select_all = select_all;
    args->is_complete = false;
    
    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    int rc = spdk_nvme_zns_offline_zone(spdk_struct->ns, spdk_struct->qpair, zslba, select_all, zns_offline_zone_cb, args);
    if (rc) {
        fprintf(stderr, "Offline zone failed!\n");
        return rc;
    }
    for (; !args->is_complete; spdk_nvme_qpair_process_completions(spdk_struct->qpair, 0));

    return 0;
}

/**
 *      The function is not done yet.
 *      zns_io_write temperarily only support write to a new LBA.
 *      Inplace update is not supported yet.
 *      The mapping mechanism is not done yet.
 */

int zns_io_append(void *payload, uint64_t zslba, uint32_t lba_count)
{   
    if (zslba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", zslba);
        return 2;
    }

    if (!payload) {
        fprintf(stderr, "Payload is NULL!\n");
        return 1;
    }

    if (lba_count > zasl) {
        fprintf(stderr, "LBA count %d is out of the range of the zone append size!\n", lba_count);
        return 3;
    }

    uint64_t z_id = zslba / nr_blocks_in_zone;
    uint64_t wp = io_map_desc->buffer_write_ptr[z_id];
    size_t data_size = lba_count << pow2_block_size;
    io_buffer_entry_t *io_buffer_entry = NULL;
    q_entry_t *q_entry = NULL;
    int rc;
    switch (io_map_desc->zone_state[z_id]) {
        case ZONE_STATE_EMPTY:
            io_map_desc->zone_state[z_id] = ZONE_STATE_IMP_OPEN;
            rc = io_buffer_q_init(&io_buffer_entry, z_id, nr_blocks_in_zone);
            if (rc) {
                //  TODO: handle error
            }
            
            io_buffer_entry_t *io_buffer_wb_entry;
            pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
            for (; io_buffer_desc->q_nums >= io_buffer_desc->q_max_nums; ) {
                rc = io_buffer_dequeue(&io_buffer_wb_entry);
                if (rc) {
                    //  TODO: handle error
                    break;
                }

                rc = io_buffer_wb_zone(io_buffer_wb_entry);
                if (rc) {
                    //  TODO: handle error
                    break;
                }
            }

            rc = io_buffer_enqueue(io_buffer_entry);
            if (rc) {
                //  TODO: handle error
            }
           
            rc = io_buffer_q_enqueue(io_buffer_entry->q_desc_p, &q_entry, payload, lba_count);
            if (rc) {
                // TODO: handle error
            }
            
            io_map_desc->io_map[wp].identifier = 0x1;
            io_map_desc->io_map[wp].q_entry = q_entry;
            io_map_desc->buffer_write_ptr[z_id] += lba_count;

            pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);

            break;

        case ZONE_STATE_IMP_OPEN:
        case ZONE_STATE_EXP_OPEN:
            rc = io_buffer_q_find(&io_buffer_entry, z_id);
            if (!rc) {
                fprintf(stderr, "Zone %ld was not found in the io buffer!\n", z_id);
                return 5;
            }
            
            if (io_buffer_entry->q_desc_p->q_size + lba_count > io_buffer_entry->q_desc_p->q_size_max) {
                fprintf(stderr, "Zone %ld doesn't have enough capacity!\n", z_id);
                fprintf(stderr, "q_size is %ld, q_size_max is %ld\n", io_buffer_entry->q_desc_p->q_size, io_buffer_entry->q_desc_p->q_size_max);
                return 4;
            }

            pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
            
            rc = io_buffer_q_enqueue(io_buffer_entry->q_desc_p, &q_entry, payload, lba_count);
            if (rc) {
                // TODO: handle error
            }

            io_map_desc->io_map[wp].identifier = 0x1;
            io_map_desc->io_map[wp].q_entry = q_entry;
            io_map_desc->buffer_write_ptr[z_id] += lba_count;

            pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);

            io_buffer_upsert(io_buffer_entry);

            break;

        case ZONE_STATE_CLOSED:
            io_map_desc->zone_state[z_id] = ZONE_STATE_IMP_OPEN;
            rc = io_buffer_q_find(&io_buffer_entry, z_id);
            if (rc) {
                if (io_buffer_entry->q_desc_p->q_size + lba_count >= io_buffer_entry->q_desc_p->q_size_max) {
                    fprintf(stderr, "Zone %ld doesn't have enough capacity!\n", z_id);
                    return 4;
                }

                pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
                
                rc = io_buffer_q_enqueue(io_buffer_entry->q_desc_p, &q_entry, payload, lba_count);
                if (rc) {
                    // TODO: handle error
                }

                io_map_desc->io_map[wp].identifier = 0x1;
                io_map_desc->io_map[wp].q_entry = q_entry;
                io_map_desc->buffer_write_ptr[z_id] += lba_count;

                pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);

                io_buffer_upsert(io_buffer_entry);

            } else {
                uint64_t available_blocks = nr_blocks_in_zone - (io_map_desc->zns_write_ptr[z_id] - zslba);
                rc = io_buffer_q_init(&io_buffer_entry, z_id, available_blocks);
                if (rc) {
                    //  TODO: handle error
                }

                io_buffer_entry_t *io_buffer_wb_entry;
                pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
                for (; io_buffer_desc->q_nums >= io_buffer_desc->q_max_nums; ) {
                    rc = io_buffer_dequeue(&io_buffer_wb_entry);
                    if (rc) {
                        //  TODO: handle error
                        break;
                    }

                    rc = io_buffer_wb_zone(io_buffer_wb_entry);
                    if (rc) {
                        //  TODO: handle error
                        break;
                    }
                }
                
                rc = io_buffer_q_enqueue(io_buffer_entry->q_desc_p, &q_entry, payload, lba_count);
                if (rc) {
                    //  TODO: handle error
                }

                io_map_desc->io_map[wp].identifier = 0x1;
                io_map_desc->io_map[wp].q_entry = q_entry;
                io_map_desc->buffer_write_ptr[z_id] += lba_count;

                rc = io_buffer_enqueue(io_buffer_entry);
                if (rc) {
                    //  TODO: handle error
                }

                pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
            }

            break;

        case ZONE_STATE_FULL:
            fprintf(stderr, "Zone %ld is full!\n", z_id);
            return 4;
        case ZONE_STATE_READONLY:
            fprintf(stderr, "Zone %ld is read only!\n", z_id);
            return 5;
        case ZONE_STATE_OFFLINE:
            fprintf(stderr, "Zone %ld is offline!\n", z_id);
            return 6;
        default:
            fprintf(stderr, "Zone %ld is in an unknown state!\n", z_id);
            return 7;
    }

    //  TODO
    return 0;
}


/**
 *      payload is a parameter that points to a pointer to the data buffer.
 */

int zns_io_read(void **payload, uint64_t lba, uint32_t lba_count)
{
    if (lba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", lba);
        return 2;
    }

    size_t data_size = lba_count << pow2_block_size;
    if (!*payload) {
        fprintf(stderr, "The payload does not exist!\n");
        return 4;
    }

    int rc;
    q_entry_t *q_entry;
    switch (io_map_desc->io_map[lba].identifier)
    {
    case 0x0:
        /* The data is not in ZNS neither io_buffer */
        fprintf(stderr, "LBA %lx is not in the io_map!\n", lba);
        return 2;
    case 0x1:
        /* The data is in io_buffer */
        q_entry = io_map_desc->io_map[lba].q_entry;
        *payload = q_entry->payload;
        io_buffer_q_upsert(q_entry);
        io_buffer_upsert(q_entry->q_desc_p->io_buffer_entry_p);
        break;
    case 0x2:
        /* The data is in ZNS */
        /* The data won't be buffered */
        bool is_complete = false;
        rc = spdk_nvme_ns_cmd_read(spdk_struct->ns, spdk_struct->qpair, *payload, io_map_desc->io_map[lba].lba, lba_count, zns_read_cb, &is_complete, 0);
        if (rc) {
            //  TODO: handle error
        }
        for (; !is_complete; spdk_nvme_qpair_process_completions(spdk_struct->qpair, 0));

        break;
    case 0x3:
        /* The data is in ZNS and io_buffer */
        q_entry = io_map_desc->io_map[lba].q_entry;
        *payload = q_entry->payload;
        io_buffer_q_upsert(q_entry);
        io_buffer_upsert(q_entry->q_desc_p->io_buffer_entry_p);
        break;
    case 0x11:
        
        //  TODO

        break;
    default:
        fprintf(stderr, "Unknown identifier!\n");
        return 3;
    }

    //  TODO
    return 0;
}