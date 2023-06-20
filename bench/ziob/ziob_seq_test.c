#include "zns_io.h"
#include <spdk/env.h>
#include <sys/time.h>

static char *g_opt_name = "ZIOB sequential I/O test";
static char *file_path = ".";
static void (*test_task)(void) = NULL;
static uint32_t g_exe_times = 1;

static size_t g_block_size = 0;
static uint64_t g_nr_blocks_in_zone = 0;
static uint64_t g_nr_zones = 0;

static uint32_t g_qd = 256;
static uint32_t g_zasl = 0;
static uint32_t g_nr_io_blocks = 1;
static size_t g_io_size = 0;
static uint64_t g_nr_test_zone = 1;

static struct timespec starttime = {0, 0}, endtime = {0, 0};
static double t_avg[512] = {0.0};

static void _append(uint64_t zslba, uint64_t nums)
{
    uint64_t z_id = zslba / g_nr_blocks_in_zone;
    char *testdata = NULL;

    int rc;
    for (uint64_t i = 0; i < nums; i++) {
        testdata = (char *)zns_io_malloc(g_io_size, zslba);
        memset(testdata, 0, g_io_size);
        snprintf(testdata, g_io_size, "This is test data %lu for zone %lu\n", i, z_id);
        rc = zns_io_append(testdata, zslba, g_nr_io_blocks);
        if (rc) {
            fprintf(stderr, "Append data to zone %lu failed\n", z_id);
            zns_env_fini();
            exit(1);
        }
    }
    zns_wait_io_complete();
}

static void append_test(FILE *fp, uint64_t num)
{
    int rc = zns_reset_zone(0, true);
    if (rc) {
        fprintf(stderr, "Reset zone failed\n");
        zns_env_fini();
        exit(1);
    }

    char fs_str[64] = {0};
    double t_used = 0.0, t_total = 0.0;

    for (uint64_t i = 0; i < g_nr_test_zone; i++) {
        clock_gettime(CLOCK_REALTIME, &starttime);
        _append(i * g_nr_blocks_in_zone, num);
        clock_gettime(CLOCK_REALTIME, &endtime);
        t_used = 1000000000*(endtime.tv_sec - starttime.tv_sec) + endtime.tv_nsec - starttime.tv_nsec;
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
    for (uint64_t i = 0; i < g_exe_times; i++)
        append_test(fp, num);
    
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

static void _read(uint64_t zslba, uint64_t nums, char *testdata)
{
    uint64_t z_id = zslba / g_nr_blocks_in_zone;
    uint64_t slba = zslba;
    char *dp = testdata;
    int rc;
    for (uint64_t i = 0; i < nums; i++) {
        rc = zns_io_read(&dp, slba, g_nr_io_blocks);
        if (rc) {
            fprintf(stderr, "Read data from zone %lu failed\n", z_id);
            zns_env_fini();
            exit(1);
        }
        dp += g_io_size;
        slba += g_nr_io_blocks;
    }
    zns_wait_io_complete();
}

static void read_test(FILE *fp, uint64_t num)
{
    char fs_str[64] = {0};
    double t_used = 0.0, t_total = 0.0;

    char *testdata = spdk_dma_zmalloc(g_nr_blocks_in_zone * g_block_size, g_block_size, NULL);
    if (!testdata) {
        fprintf(stderr, "Allocate memory for test data failed\n");
        zns_env_fini();
        exit(1);
    }

    for (uint64_t i = 0; i < g_nr_test_zone; i++) {
        memset(testdata, 0, g_nr_blocks_in_zone * g_block_size);
        clock_gettime(CLOCK_REALTIME, &starttime);
        _read(i * g_nr_blocks_in_zone, num, testdata);
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

    spdk_dma_free(testdata);
}

static void read_task(void)
{
    char fs_str[64] = {0};
    snprintf(fs_str, sizeof(fs_str), "%s/%uk_qd%u_read.txt",
                         file_path, g_nr_io_blocks << 2, g_qd);
    FILE *fp = fopen(fs_str, "w");

    uint64_t num = g_nr_blocks_in_zone / g_nr_io_blocks;
    for (uint64_t i = 0; i < g_exe_times; i++)
        read_test(fp, num);
    
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

static int init(struct spdk_env_opts *opts, struct spdk_nvme_transport_id *trid)
{
    int rc = zns_env_init(opts, g_opt_name, trid, 1, g_qd);
    if (rc) {
        printf("env initialize is failed!!!\n");
        return rc;
    }

    g_block_size = zns_get_block_size();
    g_nr_blocks_in_zone = zns_get_nr_blocks_in_zone();
    g_nr_zones = zns_get_nr_zones();
    g_zasl = zns_get_zone_append_size_limit();

    g_nr_io_blocks = (g_nr_io_blocks >= g_zasl) ? g_zasl : g_nr_io_blocks;
    g_io_size = g_nr_io_blocks * g_block_size;
    g_nr_test_zone = (g_nr_test_zone >= g_nr_zones) ? g_nr_zones : g_nr_test_zone;

    return 0;
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
                g_qd = optarg ? atoi(optarg) : g_qd;
                break;
            
            case 'b':
                g_nr_io_blocks = optarg ? atoi(optarg) : g_nr_io_blocks;
                if (g_nr_io_blocks & (g_nr_io_blocks -1)) {
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
                g_nr_test_zone = optarg ? atoi(optarg) : g_nr_test_zone;
                break;
            
            case 'p':
                file_path = optarg;
                break;
            
            case 'n':
                g_exe_times = optarg ? atoi(optarg) : g_exe_times;
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
    
    struct spdk_env_opts opts;
    struct spdk_nvme_transport_id trid = {};

    rc = init(&opts, &trid);
    if (rc)
        return rc;
    
    if (!test_task) {
        fprintf(stderr, "No test task specified\n");
        return 0;
    }
    
    test_task();

    return 0;
}