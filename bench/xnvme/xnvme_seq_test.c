/*  The program is refer to xNVMe example `Asynchronous IO`*/
#include <stdio.h>
#include <errno.h>
#include <libxnvme.h>
#include <libxnvme_pp.h>
#include <libxnvme_nvm.h>
#include <libxnvme_lba.h>
#include <libxnvmec.h>
#include <libxnvme_util.h>
#include <time.h>

#define DEFAULT_QD 256

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

static int sub_seq_write(struct xnvmec *cli)
{
    struct xnvme_dev *dev = cli->args.dev;
    const struct xnvme_geo *geo = cli->args.geo;

    uint32_t qd = cli->given[XNVMEC_OPT_QDEPTH] ? cli->args.qdepth : DEFAULT_QD;
    uint32_t nsid = cli->given[XNVMEC_OPT_NSID] ? cli->args.nsid : xnvme_dev_get_nsid(dev);

    int rc;
    struct xnvme_lba_range rng = {0};
    rng = xnvme_lba_range_from_slba_elba(dev, cli->args.slba, cli->args.elba);
    if (!rng.attr.is_valid) {
        rc = -EINVAL;
        xnvmec_perr("invalid range", rc);
        xnvme_lba_range_pr(&rng, XNVME_PR_DEF);
        return rc;
    }

    xnvmec_pinf("Write uri: '%s', qd: %d", cli->args.uri, qd);
    xnvme_lba_range_pr(&rng, XNVME_PR_DEF);

    xnvmec_pinf("Allocating and filling buf of nbytes: %zu", rng.nbytes);
    char *buf = (char *)xnvme_buf_alloc(dev, rng.nbytes);
    if (!buf) {
        rc = -errno;
        xnvmec_perr("xnvme_buf_alloc()", rc);
        goto exit;
    }

    rc = xnvmec_buf_fill(buf, rng.nbytes, "anum");
    if (rc) {
        xnvmec_perr("xnvme_buf_fill()", rc);
        goto exit;
    }

    xnvmec_pinf("Initializing queue and setting default callback function and arguments");
    struct xnvme_queue *queue = NULL;
    rc = xnvme_queue_init(dev, qd, 0, &queue);
    if (rc) {
        xnvmec_perr("xnvme_queue_init()", rc);
        goto exit;
    }

    struct cb_args cb_args = {0};
    xnvme_queue_set_cb(queue, cb_pool, &cb_args);

    xnvmec_timer_start(cli);

    char *payload = buf;
    struct xnvme_cmd_ctx *ctx;
    for (uint64_t sect = 0; (sect < rng.naddrs) && !cb_args.ecount; sect++) {
        ctx = xnvme_queue_get_cmd_ctx(queue);

submit:
        rc = xnvme_nvm_write(ctx, nsid, rng.slba + sect, 0, payload, NULL);
        switch (rc) {
            case 0:
                cb_args.submitted++;
                break;
        
            case -EBUSY:
            case -EAGAIN:
                xnvme_queue_poke(queue, 0);
                goto submit;
        
            default:
                xnvmec_perr("xnvme_nvm_write()", rc);
                xnvme_queue_put_cmd_ctx(queue, ctx);
                goto exit;
        }
next:
        payload += geo->nbytes;
    }

    rc = xnvme_queue_drain(queue);
    if (rc < 0) {
        xnvmec_perr("xnvme_queue_drain()", rc);
        goto exit;
    }

    xnvmec_timer_stop(cli);

    if (cb_args.ecount) {
        rc = -EIO;
        xnvmec_perr("Got completion errors", rc);
        goto exit;
    }

    xnvmec_timer_bw_pr(cli, "Wall-clock", rng.nbytes);

exit:
    xnvmec_pinf("cb_args: {ecount: %d, submitted: %d, completed: %d}", cb_args.ecount, cb_args.submitted, cb_args.completed);

    if (queue) {
        int rc_exit = xnvme_queue_term(queue);
        if (rc_exit)
            xnvmec_perr("xnvme_queue_term()", rc_exit);
    }
    xnvme_buf_free(dev, buf);

    return rc < 0 ? rc : 0;
}

static int sub_seq_read(struct xnvmec *cli)
{
    struct xnvme_dev *dev = cli->args.dev;
    const struct xnvme_geo *geo = cli->args.geo;

    uint32_t qd = cli->given[XNVMEC_OPT_QDEPTH] ? cli->args.qdepth : DEFAULT_QD;
    uint32_t nsid = cli->given[XNVMEC_OPT_NSID] ? cli->args.nsid : xnvme_dev_get_nsid(dev);

    int rc;
    struct xnvme_lba_range rng = {0};
    rng = xnvme_lba_range_from_slba_elba(dev, cli->args.slba, cli->args.elba);
    if (!rng.attr.is_valid) {
        rc = -EINVAL;
        xnvmec_perr("invalid range", rc);
        xnvme_lba_range_pr(&rng, XNVME_PR_DEF);
        return rc;
    }

    xnvmec_pinf("Read uri: '%s', qd: %d", cli->args.uri, qd);
    xnvme_lba_range_pr(&rng, XNVME_PR_DEF);

    //xnvmec_pinf("Allocating and filling buf of nbytes: %zu", rng.nbytes);
    char *buf = (char *)xnvme_buf_alloc(dev, rng.nbytes);
    if (!buf) {
        rc = -errno;
        xnvmec_perr("xnvme_buf_alloc()", rc);
        goto exit;
    }

    rc = xnvmec_buf_fill(buf, rng.nbytes, "zero");
    if (rc) {
        xnvmec_perr("xnvme_buf_fill()", rc);
        goto exit;
    }

    xnvmec_pinf("Initializing queue and setting default callback function and arguments");
    struct xnvme_queue *queue = NULL;
    rc = xnvme_queue_init(dev, qd, 0, &queue);
    if (rc) {
        xnvmec_perr("xnvme_queue_init()", rc);
        goto exit;
    }

    struct cb_args cb_args = {0};
    xnvme_queue_set_cb(queue, cb_pool, &cb_args);

    xnvmec_timer_start(cli);

    char *payload = buf;
    struct xnvme_cmd_ctx *ctx;
    for (uint64_t curs = 0; (curs < rng.naddrs) && !cb_args.ecount; curs++) {
        ctx = xnvme_queue_get_cmd_ctx(queue);

submit:
        rc = xnvme_nvm_read(ctx, nsid, rng.slba + curs, 0, payload, NULL);
        switch (rc) {
            case 0:
                cb_args.submitted++;
                break;
        
            case -EBUSY:
            case -EAGAIN:
                xnvme_queue_poke(queue, 0);
                goto submit;
        
            default:
                xnvmec_perr("xnvme_nvm_read()", rc);
                xnvme_queue_put_cmd_ctx(queue, ctx);
                goto exit;
        }
next:
        payload += geo->nbytes;
    }

    rc = xnvme_queue_drain(queue);
    if (rc < 0) {
        xnvmec_perr("xnvme_queue_drain()", rc);
        goto exit;
    }

    xnvmec_timer_stop(cli);

    if (cb_args.ecount) {
        rc = -EIO;
        xnvmec_perr("Got completion errors", rc);
        goto exit;
    }

    xnvmec_timer_bw_pr(cli, "Wall-clock", rng.nbytes * geo->nbytes);

    if (cli->args.data_output) {
        xnvmec_pinf("Dumping nbytes: %zu, to: '%s'", rng.nbytes, cli->args.data_output);

        rc = xnvmec_buf_to_file(buf, rng.nbytes, cli->args.data_output);
        if (rc)
            xnvmec_perr("xnvme_buf_to_file()", rc);
    }

exit:
    xnvmec_pinf("cb_args: {ecount: %d, submitted: %d, completed: %d}", cb_args.ecount, cb_args.submitted, cb_args.completed);

    if (queue) {
        int rc_exit = xnvme_queue_term(queue);
        if (rc_exit)
            xnvmec_perr("xnvme_queue_term()", rc_exit);
    }
    xnvme_buf_free(dev, buf);

    return rc < 0 ? rc : 0;
}

static struct xnvmec_sub g_subs[] = {
    {
        "seq_write",
        "Write to specific lba range sequentially",
        "Write to specific lba range sequentially [slba,elba]",
        sub_seq_write,
        {
            {XNVMEC_OPT_POSA_TITLE, XNVMEC_SKIP},
            {XNVMEC_OPT_URI, XNVMEC_POSA},

            {XNVMEC_OPT_NON_POSA_TITLE, XNVMEC_SKIP},
            {XNVMEC_OPT_QDEPTH, XNVMEC_LOPT},
            {XNVMEC_OPT_SLBA, XNVMEC_LREQ},
            {XNVMEC_OPT_ELBA, XNVMEC_LREQ},

            XNVMEC_ASYNC_OPTS,
        },
    },

    {
        "seq_read",
        "Read from specific lba range sequentially",
        "Read from specific lba range sequentially [slba,elba]",
        sub_seq_read,
        {
            {XNVMEC_OPT_POSA_TITLE, XNVMEC_SKIP},
            {XNVMEC_OPT_URI, XNVMEC_POSA},

            {XNVMEC_OPT_NON_POSA_TITLE, XNVMEC_SKIP},
            {XNVMEC_OPT_QDEPTH, XNVMEC_LOPT},
            {XNVMEC_OPT_DATA_INPUT, XNVMEC_LOPT},
            {XNVMEC_OPT_SLBA, XNVMEC_LREQ},
            {XNVMEC_OPT_ELBA, XNVMEC_LREQ},

            XNVMEC_ASYNC_OPTS,
        },
    },
};

static struct xnvmec g_cli = {
    .title = "xNVMe sequential I/O test",
    .descr_short = "xNVMe sequential I/O (read, write) performance test",
    .descr_long = "xNVMe sequential I/O (read, write) performance test",
    .nsubs = sizeof(g_subs) / sizeof(*g_subs),
    .subs = g_subs,
};

int main(int argc, char **argv)
{
    return xnvmec(&g_cli, argc, argv, XNVMEC_INIT_DEV_OPEN);
}