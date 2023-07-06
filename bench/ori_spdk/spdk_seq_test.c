#include <spdk/stdinc.h>
#include <spdk/env.h>
#include <spdk/nvme.h>
#include <spdk/nvme_zns.h>

typedef struct spdk_nvme_struct_t {
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_nvme_qpair *qpair;
} spdk_nvme_struct_t;

static spdk_nvme_struct_t *g_spdk_nvme_struct;

static char *g_opt_name = "xNVMe sequential I/O test";
static char *file_path = ".";
static char *g_buf = NULL;
static void (*test_task)(void) = NULL;
static uint32_t g_exe_times = 1;

static size_t g_block_size = 0;
static uint64_t g_nr_blocks_in_zone = 0;
static uint64_t g_nr_zones = 0;

static uint32_t g_qd = 0;
static uint32_t g_outstanding = 0;

static uint32_t g_zasl = 0;
static uint32_t g_nr_io_blocks = 1;
static size_t g_io_size = 0;
static uint64_t g_nr_test_zone = 1;

static struct timespec starttime = {0, 0}, endtime = {0, 0};
static double t_avg[512] = {0.0};

static void io_cb(void *arg, const struct spdk_nvme_cpl *cpl)
{
    //char *testdata = (char *)arg;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(g_spdk_nvme_struct->qpair,
                         (struct spdk_nvme_cpl *)cpl);
        fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&cpl->status));
        fprintf(stderr, "I/O failed, aborting run!\n");
        g_outstanding--;
        exit(1);
    }
/*
    if (testdata)
        spdk_free(testdata);
*/
    g_outstanding--;
}

static void _append(uint64_t zslba, uint64_t nums, uint32_t lba_count)
{
    uint64_t z_id = zslba / g_nr_blocks_in_zone;
    char *testdata = g_buf;

    int rc;
    for (uint64_t i = 0; i < nums; i++) {
        //testdata = (char *)spdk_dma_zmalloc(g_io_size, g_block_size, NULL);
        memset(testdata, 0, g_io_size);
        snprintf(testdata, g_io_size, "This is test data %ld for zone %ld\n", i, z_id);
        g_outstanding++;
        for (; g_outstanding > g_qd;
                         spdk_nvme_qpair_process_completions(g_spdk_nvme_struct->qpair, 0));
        rc = spdk_nvme_zns_zone_append(g_spdk_nvme_struct->ns, g_spdk_nvme_struct->qpair,
                        testdata, zslba, lba_count, io_cb, testdata, 0);
        if (rc) {
            fprintf(stderr, "Append data failed\n");
            return;
        }
        testdata += g_io_size;
    }
    for (; g_outstanding; spdk_nvme_qpair_process_completions(g_spdk_nvme_struct->qpair, 0));
}

static void append_test(FILE *fp, uint64_t num)
{
    g_outstanding++;
    int rc = spdk_nvme_zns_reset_zone(g_spdk_nvme_struct->ns, g_spdk_nvme_struct->qpair,
                         0, true, io_cb, NULL);
    if (rc) {
        fprintf(stderr, "Reset zone failed\n");
        return;
    }
    for (; g_outstanding; spdk_nvme_qpair_process_completions(g_spdk_nvme_struct->qpair, 0));

    char fs_str[64] = {0};
    double t_used = 0.0, t_total = 0.0;

    for (uint64_t i = 0; i < g_nr_test_zone; i++) {
        clock_gettime(CLOCK_REALTIME, &starttime);
        _append(i * g_nr_blocks_in_zone, num, g_nr_io_blocks);
        clock_gettime(CLOCK_REALTIME, &endtime);
        t_used = 1000000000 * (endtime.tv_sec - starttime.tv_sec) + endtime.tv_nsec - starttime.tv_nsec;
        t_used /= 1000000;
        snprintf(fs_str, sizeof(fs_str), "%f\t", t_used);
        fwrite(fs_str, strlen(fs_str), 1, fp);
        t_total += t_used;
        t_avg[i] += t_used;
    }
    snprintf(fs_str, sizeof(fs_str), "%f\n", t_total);
    fwrite(fs_str, strlen(fs_str), 1, fp);
}

static void append_task(void)
{
    char fs_str[64] = {0};
    snprintf(fs_str, sizeof(fs_str), "%s/%uk_qd%u_append.txt",
                         file_path, g_nr_io_blocks << 2, g_qd);
    FILE *fp = fopen(fs_str, "w");

    uint64_t num = g_nr_blocks_in_zone / g_nr_io_blocks;
    for (uint64_t i = 0; i < g_exe_times; i++) {
        append_test(fp, num);
    }

    double t_total = 0.0;
    for (uint64_t i = 0; i < g_nr_test_zone; i++) {
        t_avg[i] /= g_exe_times;
        snprintf(fs_str, sizeof(fs_str), "%f\t", t_avg[i]);
        fwrite(fs_str, strlen(fs_str), 1, fp);
        t_total += t_avg[i];
    }
    snprintf(fs_str, sizeof(fs_str), "%f\n", t_total);
    fwrite(fs_str, strlen(fs_str), 1, fp);

    t_total /= g_nr_test_zone;
    snprintf(fs_str, sizeof(fs_str), "%f\n", num / t_total);
    fwrite(fs_str, strlen(fs_str), 1, fp);

    fclose(fp);
}

static void _read(uint64_t zslba, uint64_t nums, uint32_t lba_count)
{
    char *testdata = g_buf;

    int rc;
    for (uint64_t i = 0; i < nums; i++) {
        //testdata = (char *)spdk_dma_zmalloc(g_io_size, g_block_size, NULL);
        g_outstanding++;
        for (; g_outstanding > g_qd;
                         spdk_nvme_qpair_process_completions(g_spdk_nvme_struct->qpair, 0));
        rc = spdk_nvme_ns_cmd_read(g_spdk_nvme_struct->ns, g_spdk_nvme_struct->qpair,
                        testdata, zslba + (i * lba_count), lba_count, io_cb, testdata, 0);
        if (rc) {
            fprintf(stderr, "Read data failed\n");
            return;
        }
        testdata += g_io_size;
    }
    for (; g_outstanding; spdk_nvme_qpair_process_completions(g_spdk_nvme_struct->qpair, 0));
}

static void read_test(FILE *fp, uint64_t num)
{
    char fs_str[64] = {0};
    double t_used = 0.0, t_total = 0.0;
    
    for (uint64_t i = 0; i < g_nr_test_zone; i++) {
        clock_gettime(CLOCK_REALTIME, &starttime);
        _read(i * g_nr_blocks_in_zone, num, g_nr_io_blocks);
        clock_gettime(CLOCK_REALTIME, &endtime);
        t_used = 1000000000 * (endtime.tv_sec - starttime.tv_sec) + endtime.tv_nsec - starttime.tv_nsec;
        t_used /= 1000000;
        snprintf(fs_str, sizeof(fs_str), "%f\t", t_used);
        fwrite(fs_str, strlen(fs_str), 1, fp);
        t_total += t_used;
        t_avg[i] += t_used;
    }
    snprintf(fs_str, sizeof(fs_str), "%f\n", t_total);
    fwrite(fs_str, strlen(fs_str), 1, fp);
}

static void read_task(void)
{
    char fs_str[64] = {0};
    snprintf(fs_str, sizeof(fs_str), "%s/%uk_qd%u_read.txt",
                         file_path, g_nr_io_blocks << 2, g_qd);
    FILE *fp = fopen(fs_str, "w");

    uint64_t num = g_nr_blocks_in_zone / g_nr_io_blocks;
    for (uint64_t i = 0; i < g_exe_times; i++) {
        read_test(fp, num);
    }

    double t_total = 0.0;
    for (uint64_t i = 0; i < g_nr_test_zone; i++) {
        t_avg[i] /= g_exe_times;
        snprintf(fs_str, sizeof(fs_str), "%f\t", t_avg[i]);
        fwrite(fs_str, strlen(fs_str), 1, fp);
        t_total += t_avg[i];
    }
    snprintf(fs_str, sizeof(fs_str), "%f\n", t_total);
    fwrite(fs_str, strlen(fs_str), 1, fp);

    t_total /= g_nr_test_zone;
    snprintf(fs_str, sizeof(fs_str), "%f\n", num / t_total);
    fwrite(fs_str, strlen(fs_str), 1, fp);

    fclose(fp);
}

static void probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
                     struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "\nAttaching to %s\n", trid->traddr);
    return;
}

static void attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
                     struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "Attached to %s\n", trid->traddr);

    g_spdk_nvme_struct->ns = spdk_nvme_ctrlr_get_ns(ctrlr, 1);
    if (!g_spdk_nvme_struct->ns) {
        fprintf(stderr, "spdk_nvme_ctrlr_get_ns() failed.\n");
        return;
    }

    if (!spdk_nvme_ns_is_active(g_spdk_nvme_struct->ns))
        fprintf(stderr, "Namespace is not active.\n");
    
    g_spdk_nvme_struct->ctrlr = ctrlr;
}

static int init(struct spdk_env_opts *opts, struct spdk_nvme_transport_id *trid)
{
    spdk_env_opts_init(opts);
    opts->name = g_opt_name;

    spdk_nvme_trid_populate_transport(trid, SPDK_NVME_TRANSPORT_PCIE);
    snprintf(trid->subnqn, sizeof(trid->subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);

    if (spdk_env_init(opts) < 0) {
        fprintf(stderr, "Unable to initialize SPDK environment.\n");
        return 1;
    }

    if (spdk_nvme_probe(trid, NULL, probe_cb, attach_cb, NULL)) {
        fprintf(stderr, "spdk_nvme_probe() failed.\n");
        return 1;
    }

    struct spdk_nvme_io_qpair_opts qpair_opts;
    spdk_nvme_ctrlr_get_default_io_qpair_opts(g_spdk_nvme_struct->ctrlr, &qpair_opts,
                         sizeof(qpair_opts));
    
    g_spdk_nvme_struct->qpair = spdk_nvme_ctrlr_alloc_io_qpair(g_spdk_nvme_struct->ctrlr,
                         &qpair_opts, 0);
    if(!g_spdk_nvme_struct->qpair) {
        fprintf(stderr, "Allocate qpair failed.\n");
        return 1;
    }

    if ((g_qd >= qpair_opts.io_queue_size) || (g_qd == 0))
        g_qd = qpair_opts.io_queue_size;
    
    g_block_size = spdk_nvme_ns_get_sector_size(g_spdk_nvme_struct->ns);
    g_nr_blocks_in_zone = spdk_nvme_zns_ns_get_zone_size_sectors(g_spdk_nvme_struct->ns);
    g_nr_zones = spdk_nvme_zns_ns_get_num_zones(g_spdk_nvme_struct->ns);
    g_zasl = spdk_nvme_zns_ctrlr_get_max_zone_append_size(g_spdk_nvme_struct->ctrlr) / g_block_size;

    g_nr_io_blocks = (g_nr_io_blocks >= g_zasl) ? g_zasl : g_nr_io_blocks;
    g_io_size = g_nr_io_blocks * g_block_size;
    g_nr_test_zone = (g_nr_test_zone >= g_nr_zones) ? g_nr_zones : g_nr_test_zone;

    g_buf = spdk_dma_malloc(g_nr_blocks_in_zone * g_block_size, g_block_size, NULL);
    if (!g_buf)
        return 1;

    return 0;
}

static void cleanup(void)
{
    struct spdk_nvme_detach_ctx *detach_ctx = NULL;
    spdk_nvme_detach_async(g_spdk_nvme_struct->ctrlr, &detach_ctx);
    if (detach_ctx)
        spdk_nvme_detach_poll(detach_ctx);

    spdk_env_fini();
}

static void usage(const char *name)
{
    fprintf(stdout, "%s options\n", name);
    fprintf(stdout, "\t[-h\t|\tDisplay all the options]\n");
    fprintf(stdout, "\t[-q\t|\tSpecify queue depth]\n");
    fprintf(stdout, "\t[-b\t|\tSpecify the number of blocks per I/O]\n");
    fprintf(stdout, "\t[-w\t|\tSpecify the test task as write]\n");
    fprintf(stdout, "\t[-r\t|\tSpecify the test task as read]\n");
    fprintf(stdout, "\t[-z\t|\tSpecify the number of zones to test]\n");
    fprintf(stdout, "\t[-p\t|\tSpecify the path to store the result files]\n");
    fprintf(stdout, "\t[-n\t|\tSpecify the number of times to execute the test task]\n");
}

static int parse_args(int argc, char **argv)
{
    for (int op; (op = getopt(argc, argv, "hwrq:b:z:p:n:")) != -1;) {
        switch (op) {
            default:
                fprintf(stderr, "Unknown option %c\n", op);
                fprintf(stderr, "-h for help\n");
                return 1;
            
            case 'h':
                usage(*argv);
                break;
            
            case 'q':
                g_qd = optarg ? atoi(optarg) : 0;
                break;
            
            case 'b':
                g_nr_io_blocks = optarg ? atoi(optarg) : 1;
                if (g_nr_io_blocks & (g_nr_io_blocks - 1)) {
                    fprintf(stderr, "The number of blocks must be power of 2\n");
                    return 1;
                }
                break;
            
            case 'w':
                if (test_task) {
                    fprintf(stderr, "Multiple test tasks specified\n");
                    return 1;
                }

                test_task = append_task;
                break;
            
            case 'r':
                if (test_task) {
                    fprintf(stderr, "Multiple test tasks specified\n");
                    return 1;
                }

                test_task = read_task;
                break;
            
            case 'z':
                g_nr_test_zone = optarg ? atoi(optarg) : 1;
                break;
            
            case 'p':
                file_path = optarg;
                break;
            
            case 'n':
                g_exe_times = optarg ? atoi(optarg) : 1;
                break;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    int rc = parse_args(argc, argv);
    if (rc)
        return rc;
    
    struct spdk_env_opts opts = {};
    struct spdk_nvme_transport_id trid = {};
    g_spdk_nvme_struct = calloc(1, sizeof(spdk_nvme_struct_t));

    rc = init(&opts, &trid);
    if (rc)
        goto exit;

    if (!test_task) {
        fprintf(stderr, "No test task specified\n");
        goto exit;
    }

    test_task();

exit:
    cleanup();
    return 0;
}
