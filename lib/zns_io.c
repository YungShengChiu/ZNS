#include "zns_io.h"

//static struct spdk_env_opts g_env_opts;
//static struct spdk_nvme_transport_id g_trid = {};
static io_buffer_desc_t *io_buffer_desc;

// These variables represent the power of two of the real size
static uint8_t pow2_zone_size;
static uint8_t pow2_block_size;

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

int zns_env_init(struct spdk_env_opts *opts, struct spdk_nvme_transport_id *trid, uint32_t nsid)
{
    spdk_env_opts_init(opts);
    opts.name = "zns_io";

    spdk_nvme_trid_populate_transport(trid, SPDK_NVME_TRANSPORT_PCIE);
    snprintf(trid->subnqn, sizeof(trid->subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);
    io_buffer_desc = NULL;

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

    pow2_zone_size = 0;
    pow2_block_size = 0;
    size_t zone_size = spdk_nvme_zns_ns_get_zone_size_sectors(io_buffer_desc->ns);
    size_t block_size = spdk_nvme_ns_get_sector_size(io_buffer_desc->ns);

    for (int i = 1; i != zone_size; i <<= 1)
        pow2_zone_size++;
    
    for (int i = 1; i != block_size; i <<= 1)
        pow2_block_size++;

    //  TODO

    return rc;
}

static void release_io_buffer()
{
    if (!io_buffer_desc)
        return;
    
    //  TODO
}

void zns_env_fini(struct spdk_nvme_ctrlr *ctrlr)
{
    release_io_buffer();
    
    if (ctrlr) {
        struct spdk_nvme_detach_ctx *detach_ctx = NULL;
        spdk_nvme_detach_async(ctrlr, &detach_ctx);
        if (detach_ctx)
            spdk_nvme_detach_poll(detach_ctx);
    }

    spdk_env_fini();
}

int zns_io_write(void *payload, uint64_t zslba, uint32_t lba_count)
{
    //  TODO
    return 0;
}

int zns_io_read(void *payload, uint64_t lba, uint32_t lba_count)
{
    //  TODO
    return 0;
}