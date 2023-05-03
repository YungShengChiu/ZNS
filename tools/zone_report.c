#include <spdk/stdinc.h>
#include <spdk/env.h>
#include <spdk/nvme.h>
#include <spdk/nvme_zns.h>
#include <spdk/string.h>

typedef struct spdk_struct_t spdk_struct_t;

struct spdk_struct_t {
    struct spdk_env_opts *opts;
    struct spdk_nvme_transport_id *trid;
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_nvme_qpair *qpair;
    const struct spdk_nvme_ns_data *nsdata;
    const struct spdk_nvme_zns_ns_data *znsdata;
};

static spdk_struct_t *spdk_struct;

static size_t block_size;
static uint64_t nr_blocks_in_zone;
static uint64_t nr_zones;

static void io_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    bool *is_completed = (bool *)arg;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(spdk_struct->qpair, (struct spdk_nvme_cpl *)cpl);
        fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&cpl->status));
        fprintf(stderr, "I/O failed, aborting run!\n");
        *is_completed = true;
        exit(1);
    }

    *is_completed = true;
}

static void print_uline(char marker, int len)
{
    for (int i = 0; i < len; i++)
        putchar(marker);
    
    putchar('\n');
}

static void print_zone(uint8_t *report, uint32_t idx, uint32_t zdes)
{
    struct spdk_nvme_zns_zone_desc *desc;
    uint32_t zone_report_size = sizeof(struct spdk_nvme_zns_zone_report);
    uint32_t zone_desc_size = sizeof(struct spdk_nvme_zns_zone_desc);
    uint32_t zd_idx = zone_report_size + idx * (zone_desc_size + zdes);

    desc = (struct spdk_nvme_zns_zone_desc *)(report + zd_idx);

    printf("ZSLBA: 0x%-18" PRIx64 " ZCAP: 0x%-18" PRIx64 " WP: 0x%-18" PRIx64 " ZS: ", desc->zslba, desc->zcap, desc->wp);
    switch (desc->zs)
    {
    case SPDK_NVME_ZONE_STATE_EMPTY:
        printf("%-20s", "Empty");
        break;
    case SPDK_NVME_ZONE_STATE_IOPEN:
        printf("%-20s", "Implicit open");
        break;
    case SPDK_NVME_ZONE_STATE_EOPEN:
        printf("%-20s", "Explicit open");
        break;
    case SPDK_NVME_ZONE_STATE_CLOSED:
        printf("%-20s", "Closed");
        break;
    case SPDK_NVME_ZONE_STATE_RONLY:
        printf("%-20s", "Read only");
        break;
    case SPDK_NVME_ZONE_STATE_FULL:
        printf("%-20s", "Full");
        break;
    case SPDK_NVME_ZONE_STATE_OFFLINE:
        printf("%-20s", "Offline");
        break;
    default:
        printf("%-20s", "Unknown");
        break;
    }

    printf(" ZT: %-20s ZA: 0x%-18x\n", (desc->zt == SPDK_NVME_ZONE_TYPE_SEQWR) ? "SWR" : "Reserved", desc->za.raw);

    if (!desc->za.bits.zdev)
        return;
    
    for (uint32_t i = 0; i < zdes; i += 8)
        printf("zone_desc_ext[%d] : 0x%" PRIx64 "\n", i, *(uint64_t *)(report + zd_idx + zone_desc_size + i));
}

static void report_zone(uint64_t *nr_zones_to_report)
{
    uint32_t max_xfer_size = spdk_nvme_ns_get_max_io_xfer_size(spdk_struct->ns);
    uint8_t *report_buf = (uint8_t *)malloc(max_xfer_size);
    printf("Report zone\n");
    
    *nr_zones_to_report = (*nr_zones_to_report <= nr_zones) ? *nr_zones_to_report : nr_zones;
    print_uline('=', fprintf(stdout, "NVMe ZNS Zone Report (first %zu of %zu)\n", *nr_zones_to_report, nr_zones));

    int rc;
    uint64_t slba = 0;
    uint64_t handled_zones = 0;
    bool is_completed = false;
    memset(report_buf, 0, max_xfer_size);

    rc = spdk_nvme_zns_report_zones(spdk_struct->ns, spdk_struct->qpair, report_buf, max_xfer_size,
        slba, SPDK_NVME_ZRA_LIST_ALL, true, io_cb, &is_completed);

    if (rc) {
        fprintf(stderr, "spdk_nvme_zns_ext_report_zones() failed\n");
        exit(1);
    }
    for (; !is_completed; spdk_nvme_qpair_process_completions(spdk_struct->qpair, 0));

    for (uint64_t i = 0; handled_zones < *nr_zones_to_report; i++) {
        print_zone(report_buf, i, 0);
        slba += nr_blocks_in_zone;
        handled_zones++;
    }

    free(report_buf);
}

static void probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "\nAttaching to %s\n", trid->traddr);
    return;
}

static void attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "Attached to %s\n", trid->traddr);
        
    spdk_struct->ctrlr = ctrlr;
    spdk_struct->ns = spdk_nvme_ctrlr_get_ns(ctrlr, 1);
    if (!spdk_struct->ns) {
        fprintf(stderr, "spdk_nvme_ctrlr_get_ns() failed.\n");
        return;
    }

    if (!spdk_nvme_ns_is_active(spdk_struct->ns))
        fprintf(stderr, "Namespace is not active.\n");
}

static int init(const char *opts_name)
{
    spdk_env_opts_init(spdk_struct->opts);
    spdk_struct->opts->name = opts_name;
    
    spdk_nvme_trid_populate_transport(spdk_struct->trid, SPDK_NVME_TRANSPORT_PCIE);
    snprintf(spdk_struct->trid->subnqn, sizeof(spdk_struct->trid->subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);

    if (spdk_env_init(spdk_struct->opts) < 0) {
        fprintf(stderr, "Unable to initialize SPDK env.\n");
        return 1;
    }

    if (spdk_nvme_probe(spdk_struct->trid, NULL, probe_cb, attach_cb, NULL)) {
        fprintf(stderr, "spdk_nvme_probe() failed.\n");
        return 1;
    }

    spdk_struct->qpair = spdk_nvme_ctrlr_alloc_io_qpair(spdk_struct->ctrlr, NULL, 0);
    if (!spdk_struct->qpair) {
        fprintf(stderr, "Allocate qpair failed.\n");
        return 1;
    }

    return 0; 
}

static void get_zns_data(void)
{
    block_size = spdk_nvme_ns_get_sector_size(spdk_struct->ns);
    nr_blocks_in_zone = spdk_nvme_zns_ns_get_zone_size_sectors(spdk_struct->ns);
    nr_zones = spdk_nvme_zns_ns_get_num_zones(spdk_struct->ns);

    spdk_struct->nsdata = spdk_nvme_ns_get_data(spdk_struct->ns);
    spdk_struct ->znsdata = spdk_nvme_zns_ns_get_data(spdk_struct->ns);
}

static void cleanup(void)
{
    struct spdk_nvme_detach_ctx *detach_ctx = NULL;
    spdk_nvme_detach_async(spdk_struct->ctrlr, &detach_ctx);
    if (detach_ctx)
        spdk_nvme_detach_poll(detach_ctx);

    spdk_env_fini();
}

static void usage(const char *program_name)
{
    fprintf(stdout, "usage:\n");
    fprintf(stdout, "%s options\n\n", program_name);
    fprintf(stdout, "         '-n' to specify the number of displayed zone\n");
}

static int parse_args(int argc, char **argv, uint64_t *nr_zones_to_report)
{
    int op;
    while ((op = getopt(argc, argv, "n:")) != -1) {
        switch (op) {
        case 'n':
            *nr_zones_to_report = atoi(optarg);
            break;
        default:
            usage(*argv);
            exit(1);
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    uint64_t nr_zones_to_report;
    int rc = parse_args(argc, argv, &nr_zones_to_report);
    
    spdk_struct = (spdk_struct_t *)calloc(1, sizeof(spdk_struct_t));
    spdk_struct->opts = (struct spdk_env_opts *)malloc(sizeof(struct spdk_env_opts));
    spdk_struct->trid = (struct spdk_nvme_transport_id *)malloc(sizeof(struct spdk_nvme_transport_id));

    rc = init("zone report tool");
    if (rc) {
        fprintf(stderr, "init_env() failed.\n");
        goto exit;
    }

    get_zns_data();
    report_zone(&nr_zones_to_report);

exit:
    cleanup();
    return rc;
}