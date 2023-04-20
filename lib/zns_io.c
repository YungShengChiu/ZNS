#include "zns_io.h"
#include "zns_io_map.h"

// These variables represent the power of two of the real size
static uint64_t nr_blocks_in_zone;
static uint64_t nr_blocks_in_ns;
static size_t block_size;
static uint64_t nr_zones;
static uint32_t zasl;
static uint8_t pow2_block_size;

struct spdk_env_opts *zns_env_opts = NULL;
struct spdk_nvme_transport_id *zns_trid = NULL;
io_buffer_desc_t *io_buffer_desc = NULL;
io_map_desc_t *io_map_desc;

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
    *(bool *)arg = true;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(io_buffer_desc->qpair, cpl);
        //exit(1);
    }
}

static void zns_read_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    *(bool *)arg = true;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(io_buffer_desc->qpair, cpl);
        //exit(1);
    }
}

struct reset_args {
    uint64_t zslba;
    bool select_all;
};

static void zns_reset_zone_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{   
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(io_buffer_desc->qpair, cpl);
        pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
        //exit(1);
    }

    struct reset_args *args = (struct reset_args *)arg;
    io_map_reset_zone(args->zslba, args->select_all);
    io_buffer_reset_zone(args->zslba >> , args->select_all);
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
    free(args);
}

/**
 *      The function is a critical section
 */

static void io_buffer_upsert(q_entry_t *q_entry)
{
    io_buffer_entry_t *io_buffer_entry = q_entry->q_desc_p->io_buffer_entry_p;
    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    io_buffer_q_remove(q_entry);
    io_buffer_q_insert_front(q_entry);
    io_buffer_remove(io_buffer_entry);
    io_buffer_insert_front(io_buffer_entry);
    pthread_mutex_unlock(&io_buffer_desc->io_buffer_mutex);
}

static void io_buffer_release()
{
    int rc = io_buffer_free();
    if (rc)
        fprintf(stderr, "Failed to release I/O buffer!\n");
}

static void io_map_release()
{
    int rc = io_map_free();
    if (rc)
        fprintf(stderr, "Failed to release I/O map!\n");
}

int zns_env_init(struct spdk_env_opts *opts, struct spdk_nvme_transport_id *trid, uint32_t nsid)
{
    spdk_env_opts_init(opts);
    opts->name = "zns_io";
    zns_env_opts = opts;

    spdk_nvme_trid_populate_transport(trid, SPDK_NVME_TRANSPORT_PCIE);
    snprintf(trid->subnqn, sizeof(trid->subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);
    zns_trid = trid;

    int rc = spdk_env_init(opts);
    if (rc < 0) {
        zns_env_fini(NULL);
        return rc;
    }

    struct spdk_nvme_ctrlr *ctrlr;
    rc = spdk_nvme_probe(trid, &ctrlr, probe_cb, attach_cb, NULL);
    if (rc) {
        fprintf(stderr, "spdk_nvme_probe() failed!\n");
        zns_env_fini(NULL);
        return rc;
    }

    if (!ctrlr) {
        fprintf(stderr, "No NVMe controllers found!\n");
        zns_env_fini(NULL);
        return 1;
    }

    io_buffer_desc = io_buffer_new();
    if (!io_buffer_desc) {
        fprintf(stderr, "Allocate io_buffer_desc failed!\n");
        zns_env_fini(ctrlr);
        return rc;
    }
    io_buffer_desc->ctrlr = ctrlr;
    io_buffer_desc->ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
    
    if (!spdk_nvme_ns_is_active(io_buffer_desc->ns)) {
        fprintf(stderr, "Namespace %d is inactive!\n", nsid);
        zns_env_fini(ctrlr);
        return rc;
    }

    io_buffer_desc->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr, NULL, 0);
    if (!io_buffer_desc->qpair) {
        fprintf(stderr, "NVMe I/O qpair allocation failed!\n");
        return rc;
    }

    pthread_mutex_init(&io_buffer_desc_t->io_buffer_mutex, NULL);

    io_buffer_desc->q_max_nums = spdk_nvme_zns_ns_get_max_active_zones(io_buffer_desc->ns);

    block_size = spdk_nvme_ns_get_sector_size(io_buffer_desc->ns);
    nr_zones = spdk_nvme_zns_ns_get_num_zones(io_buffer_desc->ns);
    nr_blocks_in_zone = spdk_nvme_zns_ns_get_zone_size_sectors(io_buffer_desc->ns);
    nr_blocks_in_ns = spdk_nvme_ns_get_num_sectors(io_buffer_desc->ns);
    zasl = spdk_nvme_zns_ctrlr_get_max_zone_append_size(io_buffer_desc->ctrlr);

    pow2_block_size = 0;
    for (int i = 1; i != block_size; i <<= 1)
        pow2_block_size++;

    io_map_desc = io_map_new();
    if (!io_map_desc) {
        fprintf(stderr, "Allocate io_map_desc failed!\n");
        zns_env_fini(ctrlr);
        return rc;
    }
    rc = io_map_init(nr_blocks_in_ns, nr_block_in_zone, nr_zones);
    if (rc) {
        fprintf(stderr, "io_map_init() failed!\n");
        zns_env_fini(ctrlr);
        return rc;
    }

    return rc;
}

void zns_env_fini(struct spdk_nvme_ctrlr *ctrlr)
{
    release_io_buffer();
    release_io_map();
    pthread_mutex_destroy(&io_buffer_desc_t->io_buffer_mutex);

    if (ctrlr) {
        struct spdk_nvme_detach_ctx *detach_ctx = NULL;
        spdk_nvme_detach_async(ctrlr, &detach_ctx);
        if (detach_ctx)
            spdk_nvme_detach_poll(detach_ctx);
    }

    zns_env_opts = NULL;
    zns_trid = NULL;

    spdk_env_fini();
}

static int io_buffer_wb_zone(io_buffer_entry_t *io_buffer_entry)
{
    if (!io_buffer_desc || !io_map_desc)
        return 1;
    
    if (!io_buffer_entry)
        return 2;
    
    uint64_t zslba = io_buffer_entry->q_desc_p->q_id * nr_blocks_in_zone;
    uint64_t z_id = zslba / nr_blocks_in_zone;
    uint64_t lba;
    uint64_t lba_count;
    int rc;
    bool is_complete;
    q_entry_t *q_entry = NULL;
    io_buffer_q_desc_t *q_desc = io_buffer_entry->q_desc_p;
    for (uint64_t i = 0; i < nr_blocks_in_zone; i += lba_count) {
        lba = zslba + i;
        if (io_map_desc->io_map[lba].identifier == 3) {
            q_entry = io_map_desc->io_map[lba].q_entry;
            lba_count = q_entry->size;
            io_buffer_q_remove(q_entry);
            is_complete = false;
            rc = spdk_nvme_zns_zone_append(io_buffer_desc->ns, io_buffer_desc->qpair, q_entry->payload, zslba, lba_count, zns_write_cb, &is_complete, 0);
            if (rc) {
                //  TODO: handle error
            }
            for (; !is_complete;);
            spdk_free(io_buffer_q_release_entry(q_entry));
            for (uint64_t j = 0; j < lba_count; j++)
                io_map_desc->io_map[lba + j].lba = io_map_desc->write_ptr[z_id] + j;
            io_map_desc->write_ptr[z_id] += lba_count;
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

int zns_io_reset_zone(uint64_t zslba, bool select_all)
{
    if (zslba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", zslba);
        return 2;
    }

    struct reset_args *args = (struct reset_args *)malloc(sizeof(struct reset_args));
    args.zslba = zslba;
    args.select_all = select_all;

    pthread_mutex_lock(&io_buffer_desc->io_buffer_mutex);
    int rc = spdk_nvme_zns_reset_zone(io_buffer_desc->ns, io_buffer_desc->qpair, zslba, select_all, zns_reset_zone_cb, args);
    if (rc) {
        fprintf(stderr, "Reset zone failed!\n");
        free(args);
        return rc;
    }
    
    return 0;
}

/**
 *      The function is not done yet.
 *      zns_io_write temperarily only support write to a new LBA.
 *      Inplace update is not supported yet.
 *      The mapping mechanism is not done yet.
 */

int zns_io_write(void *payload, uint64_t zslba, uint32_t lba_count)
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

    uint32_t z_id = zslba >> pow2_block_size;
    size_t data_size = lba_count << pow2_block_size;
    io_buffer_entry_t *io_buffer_entry;
    void *buf = spdk_zmalloc(data_size, block_size, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
    int rc = io_buffer_q_find_or_init(&io_buffer_entry, z_id, nr_blocks_in_zone, nr_blocks_in_zone << pow2_block_size);
    switch (rc)
    {
    case 0x0:

        break;
    case 0x1:
        memcpy(buf, payload, data_size);
        io_buffer_q_desc_t *io_buffer_q_desc = io_buffer_entry->io_buffer_q_desc;
        q_entry_t *q_entry;
        if (io_buffer_q_desc->q_depth >= io_buffer_q_desc->q_depth_max) {
            rc = io_buffer_q_dequeue(io_buffer_q_desc, &q_entry);
            if (rc) {
                //  TODO
                return rc;
            }
            rc = spdk_nvme_zns_zone_append();
            //  TODO
        }
        for (; data_size + io_buffer_q_desc->q_buffer > io_buffer_q_desc->q_buffer_max;) {
            rc = io_buffer_q_dequeue(io_buffer_q_desc, &q_entry);
            if (rc) {
                //  TODO
                return rc;
            }

            rc = spdk_nvme_zns_zone_append();
            //  TODO
        }
        break;
    case 0x2:
        break;
    case 0x3:
        break;
    default:
        break;
    }

    //  TODO
    return 0;
}

int zns_io_read(void *payload, uint64_t lba, uint32_t lba_count)
{
    if (lba >= nr_blocks_in_ns) {
        fprintf(stderr, "LBA %lx is out of the range of the namespace!\n", lba);
        return 2;
    }

    size_t data_size = lba_count << pow2_block_size;
    payload = (void *)malloc(data_size);
    if (!payload) {
        fprintf(stderr, "Allocate payload failed!\n");
        return 4;
    }

    int rc;
    void *buf;
    switch (io_map_desc->io_map[lba].identifier)
    {
    case 0x0:
        /* The data is not in ZNS neither io_buffer */
        fprintf(stderr, "LBA %lx is not in the io_map!\n", lba);
        return 2;
    case 0x1:
        /* The data is in io_buffer */
        buf = io_map_desc->io_map[lba].q_entry->payload;
        memcpy(payload, buf, data_size);
        io_buffer_upsert(io_map_desc->io_map[lba].q_entry);
        break;
    case 0x2:
        /* The data is in ZNS */
        buf = spdk_zmalloc(data_size, block_size, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
        if (!buf) {
            fprintf(stderr, "Allocate buffer failed!\n");
            free(payload);
            return 4;
        }
        bool is_complete = false;
        rc = spdk_nvme_ns_cmd_read(io_buffer_desc->ns, io_buffer_desc->qpair, buf, io_map_desc->io_map[lba].data_p.lba, lba_count, zns_read_cb, &is_complete, 0);
        for (; !is_complete;);
        memcpy(payload, buf, data_size);
        spdk_free(buf);
        break;
    case 0x3:
        /* The data is in ZNS and io_buffer */
        buf = io_map_desc->io_map[lba].q_entry->payload;
        memcpy(payload, buf, data_size);
        io_buffer_upsert(io_map_desc->io_map[lba].q_entry);
        break;
    default:
        fprintf(stderr, "Unknown identifier!\n");
        return 3;
    }

    //  TODO
    return 0;
}