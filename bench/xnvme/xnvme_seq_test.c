#include <stdlib.h>
#include <errno.h>
#include <libxnvme.h>

#define DEFAULT_QD 256

static char *file_path = ".";
static char *g_buf = NULL;
static uint32_t g_exe_times = 1;

static uint32_t g_nr_io_blocks = 1;
static size_t g_io_size = 0;
static uint64_t g_nr_test_zone = 1;

static uint64_t g_slba = 0;
static uint64_t g_elba = 0;
static uint64_t g_nsid = 0;

static struct xnvme_queue *queue = NULL;

static struct timespec starttime = {0, 0}, endtime = {0, 0};
static double t_avg[512] = {0.0};

struct cb_args {
    uint32_t ecount;
    uint32_t completed;
    uint32_t submitted;
};

static void cb_pool(struct xnvme_cmd_ctx *ctx, void *cb_arg)
{
    struct cb_args *cb_args = cb_arg;

    cb_args->completed++;

    if (xnvme_cmd_ctx_cpl_status(ctx)) {
        xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
        cb_args->ecount++;
    }

    xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);
}

static int task_init(struct xnvme_cli *cli, uint32_t *qd)
{
    struct xnvme_dev *dev = cli->args.dev;
    const struct xnvme_geo *geo = cli->args.geo;
    
    *qd = cli->given[XNVME_CLI_OPT_QDEPTH] ? cli->args.qdepth : DEFAULT_QD;
    g_nsid = cli->given[XNVME_CLI_OPT_NSID] ? cli->args.nsid : xnvme_dev_get_nsid(dev);
    g_slba = cli->given[XNVME_CLI_OPT_SLBA] ? cli->args.slba : 0;
    g_elba = cli->given[XNVME_CLI_OPT_ELBA] ? cli->args.elba : geo->nsect - 1;
    g_nr_io_blocks = cli->given[XNVME_CLI_OPT_NLB] ? cli->args.nlb : g_nr_io_blocks;
    g_io_size = g_nr_io_blocks * geo->lba_nbytes;
    g_exe_times = cli->given[XNVME_CLI_OPT_CDW02] ? cli->args.cdw[2] : g_exe_times;
    g_nr_test_zone = ((g_elba + 1) - g_slba) / geo->nsect;

    size_t buf_size = geo->nsect * geo->lba_nbytes;
    g_buf = xnvme_buf_alloc(dev, buf_size);
    if (!g_buf) {
        xnvme_cli_perr("xnvme_buf_alloc()", -errno);
        return -errno;
    }

    int rc = xnvme_queue_init(dev, *qd, 0, &queue);
    if (rc) {
        xnvme_cli_perr("xnvme_queue_init()", rc);
        return rc;
    }

    return 0;
}

static int _append(struct xnvme_spec_znd_descr *zone, struct cb_args *cb_args)
{
    int rc;
    struct xnvme_cmd_ctx *ctx;
    char *payload = g_buf;
    for (uint64_t curs = 0; (curs < zone->zcap) && !cb_args->ecount; curs += g_nr_io_blocks) {
        ctx = xnvme_queue_get_cmd_ctx(queue);
        snprintf(payload, g_io_size, "This is test data %lu for zone %lu\n", curs, zone->zslba);

submit:
        rc = xnvme_znd_append(ctx, g_nsid, zone->zslba, g_nr_io_blocks - 1, payload, NULL);
        switch (rc) {
            case 0:
                cb_args->submitted++;
                break;
            
            case -EBUSY:
            case -EAGAIN:
                xnvme_queue_poke(queue, 0);
                goto submit;
            
            default:
                xnvme_cli_perr("submission-error", EIO);
                return rc;
        }

next:

        payload += g_io_size;
    }

    rc = xnvme_queue_drain(queue);
    if (rc < 0) {
        xnvme_cli_perr("xnvme_queue_drain()", rc);
        return rc;
    }

    if (cb_args->ecount) {
        rc = -EIO;
        xnvme_cli_perr("got completion errors", rc);
        return rc;
    }
    
    return 0;
}

static int _seq_append(struct xnvme_cli *cli, struct cb_args *cb_args, FILE *fp)
{
    //  Reset zone
    struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);
    int rc = xnvme_znd_mgmt_send(ctx, g_nsid, g_slba, true, XNVME_SPEC_ZND_CMD_MGMT_SEND_RESET,
                        XNVME_SPEC_ZND_OPC_MGMT_RECV, NULL);

    rc = xnvme_queue_drain(queue);
    if (rc < 0) {
        xnvme_cli_perr("xnvme_queue_drain()", rc);
        return rc;
    }

    char fs_str[64] = {0};
    double t_used = 0.0, t_total = 0.0;
    
    const struct xnvme_geo *geo = cli->args.geo;
    struct xnvme_spec_znd_descr zone = {0};
    uint64_t zslba = g_slba;
    for (uint64_t i = 0; i < g_nr_test_zone; i++) {
        rc = xnvme_znd_descr_from_dev(cli->args.dev, zslba, &zone);
        if (rc) {
            xnvme_cli_perr("xnvme_znd_descr_from_dev()", -rc);
            return rc;
        }

        clock_gettime(CLOCK_REALTIME, &starttime);
        rc = _append(&zone, cb_args);
        clock_gettime(CLOCK_REALTIME, &endtime);
        if (rc) {
            //  TODO: error handling
            return rc;
        }
        t_used = 1000000000*(endtime.tv_sec - starttime.tv_sec) + endtime.tv_nsec - starttime.tv_nsec;
        t_used /= 1000000;
        snprintf(fs_str, sizeof(fs_str), "%f\t", t_used);
        fwrite(fs_str, strlen(fs_str), 1, fp);
        t_total += t_used;
        t_avg[i] += t_used;

        zslba += geo->nsect;
    }
    snprintf(fs_str, sizeof(fs_str), "%f\n", t_total);
    fwrite(fs_str, strlen(fs_str), 1, fp);

    return 0;
}

static int sub_seq_append(struct xnvme_cli *cli)
{
    /* The entry task of append */
    struct xnvme_spec_znd_descr zone = {0};
    
    uint32_t qd = DEFAULT_QD;
    int rc = task_init(cli, &qd);
    if (rc)
        goto exit;

    char fs_str[64] = {0};
    snprintf(fs_str, sizeof(fs_str), "%s/%uk_qd%u_append.txt", file_path,
                        g_nr_io_blocks << 2, qd);
    FILE *fp = fopen(fs_str, "w");

    struct cb_args *cb_args = NULL;
    for (uint64_t i = 0; i < g_exe_times; i++) {
        cb_args = calloc(1, sizeof(cb_args));
        xnvme_queue_set_cb(queue, cb_pool, cb_args);

        rc = _seq_append(cli, cb_args, fp);
        if (rc) {
            //  TODO: error handling
            free(cb_args);
            goto exit;
        }
        free(cb_args);
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
    snprintf(fs_str, sizeof(fs_str), "%f\n", (cli->args.geo)->nsect / g_nr_io_blocks / t_total);

exit:
    fclose(fp);

    if (queue) {
        int rc_exit = xnvme_queue_term(queue);
        if (rc_exit)
            xnvme_cli_perr("xnvme_queue_term()", rc_exit);
    }
    xnvme_buf_free(cli->args.dev, g_buf);

    return rc < 0 ? rc : 0;
}

static int _read(struct xnvme_spec_znd_descr *zone, struct cb_args *cb_args)
{
    int rc;
    struct xnvme_cmd_ctx *ctx;
    char *payload = g_buf;
    for (uint64_t curs = 0; (curs < zone->zcap) && !cb_args->ecount; curs += g_nr_io_blocks) {
        ctx = xnvme_queue_get_cmd_ctx(queue);
    
submit:
        rc = xnvme_nvm_read(ctx, g_nsid, zone->zslba + curs, g_nr_io_blocks - 1, payload, NULL);
        switch (rc) {
            case 0:
                cb_args->submitted++;
                break;
            
            case -EBUSY:
            case -EAGAIN:
                xnvme_queue_poke(queue, 0);
                goto submit;
            
            default:
                xnvme_cli_perr("submission-error", EIO);
                return rc;
        }

next:
        payload += g_io_size;
    }

    rc = xnvme_queue_drain(queue);
    if (rc < 0) {
        xnvme_cli_perr("xnvme_queue_drain()", rc);
        return rc;
    }

    if (cb_args->ecount) {
        rc = -EIO;
        xnvme_cli_perr("got completion errors", rc);
        return rc;
    }

    return 0;
}

static int _seq_read(struct xnvme_cli *cli, struct cb_args *cb_args, FILE *fp)
{
    char fs_str[64] = {0};
    double t_used = 0.0, t_total = 0.0;

    int rc;
    const struct xnvme_geo *geo = cli->args.geo;
    struct xnvme_spec_znd_descr zone = {0};
    uint64_t zslba = g_slba;
    for (uint64_t i = 0; i < g_nr_test_zone; i++) {
        rc = xnvme_znd_descr_from_dev(cli->args.dev, zslba, &zone);
        if (rc) {
            xnvme_cli_perr("xnvme_znd_descr_from_dev()", -rc);
            return rc;
        }

        clock_gettime(CLOCK_REALTIME, &starttime);
        rc = _read(&zone, cb_args);
        clock_gettime(CLOCK_REALTIME, &endtime);
        if (rc) {
            //  TODO: error handling
            return rc;
        }
        t_used = 1000000000*(endtime.tv_sec - starttime.tv_sec) + endtime.tv_nsec - starttime.tv_nsec;
        t_used /= 1000000;
        snprintf(fs_str, sizeof(fs_str), "%f\t", t_used);
        fwrite(fs_str, strlen(fs_str), 1, fp);
        t_total += t_used;
        t_avg[i] += t_used;

        zslba += geo->nsect;
    }
    snprintf(fs_str, sizeof(fs_str), "%f\n", t_total);
    fwrite(fs_str, strlen(fs_str), 1, fp);

    return 0;
}

static int sub_seq_read(struct xnvme_cli *cli)
{
    /* The entry task of read */
    struct xnvme_spec_znd_descr zone = {0};

    uint32_t qd = DEFAULT_QD;
    int rc = task_init(cli, &qd);
    if (rc)
        goto exit;

    char fs_str[64] = {0};
    snprintf(fs_str, sizeof(fs_str), "%s/%uk_qd%u_read.txt", file_path,
                        g_nr_io_blocks << 2, qd);
    FILE *fp = fopen(fs_str, "w");

    struct cb_args *cb_args = NULL;
    for (uint64_t i = 0; i < g_exe_times; i++) {
        cb_args = calloc(1, sizeof(cb_args));
        xnvme_queue_set_cb(queue, cb_pool, cb_args);

        rc = _seq_read(cli, cb_args, fp);
        if (rc) {
            //  TODO: error handling
            free(cb_args);
            goto exit;
        }
        free(cb_args);
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
    snprintf(fs_str, sizeof(fs_str), "%f\n", (cli->args.geo)->nsect / g_nr_io_blocks / t_total);

exit:
    fclose(fp);

}

static struct xnvme_cli_sub g_subs[] = {
    {
        "seq_append",
        "Append to specific lba range sequentially",
        "Append to specific lba range sequentially [slba,elba]",
        sub_seq_append,
        {
            {XNVME_CLI_OPT_POSA_TITLE, XNVME_CLI_SKIP},
            {XNVME_CLI_OPT_URI, XNVME_CLI_POSA},

            {XNVME_CLI_OPT_NON_POSA_TITLE, XNVME_CLI_SKIP},
            {XNVME_CLI_OPT_QDEPTH, XNVME_CLI_LOPT},
            {XNVME_CLI_OPT_SLBA, XNVME_CLI_LOPT},
            {XNVME_CLI_OPT_ELBA, XNVME_CLI_LOPT},
            {XNVME_CLI_OPT_NLB, XNVME_CLI_LOPT},
            {XNVME_CLI_OPT_CDW02, XNVME_CLI_LOPT},

            XNVME_CLI_ASYNC_OPTS,
        },
    },
    {
        "seq_read",
        "Read from specific lba range sequentially",
        "Read from specific lba range sequentially [slba,elba]",
        sub_seq_read,
        {
            {XNVME_CLI_OPT_POSA_TITLE, XNVME_CLI_SKIP},
            {XNVME_CLI_OPT_URI, XNVME_CLI_POSA},

            {XNVME_CLI_OPT_NON_POSA_TITLE, XNVME_CLI_SKIP},
            {XNVME_CLI_OPT_QDEPTH, XNVME_CLI_LOPT},
            {XNVME_CLI_OPT_SLBA, XNVME_CLI_LOPT},
            {XNVME_CLI_OPT_ELBA, XNVME_CLI_LOPT},
            {XNVME_CLI_OPT_NLB, XNVME_CLI_LOPT},
            {XNVME_CLI_OPT_CDW02, XNVME_CLI_LOPT},

            XNVME_CLI_ASYNC_OPTS,
        },
    },
};

static struct xnvme_cli g_cli = {
    .title = "xNVMe Sequential I/O test",
    .descr_short = "xNVMe sequential I/O (Read, Append) performance test",
    .nsubs = sizeof(g_subs) / sizeof(*g_subs),
    .subs = g_subs,
};

int main(int argc, char **argv)
{
    return xnvme_cli_run(&g_cli, argc, argv, XNVME_CLI_INIT_DEV_OPEN);
}
