#include "spdk/stdinc.h"
#include "spdk/thread.h"
#include "spdk/bdev.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/string.h"
#include "spdk/bdev_zone.h"

struct context_t {
    struct spdk_bdev *bdev;
    struct spdk_bdev_desc *bdev_desc;
    struct spdk_io_channel *bdev_io_channel;
    char *bdev_name;
    char *buf;
    uint64_t buf_size;
    struct spdk_bdev_io_wait_entry bdev_io_wait;
};

static char *g_bdev_name = "Nvme0n1";
static uint64_t zone_id;
static uint64_t num_blocks;

static void release(void *arg, int stop_rc)
{
    struct context_t *context = (struct context_t *)arg;
    if (context->bdev_io_channel)
        spdk_put_io_channel(context->bdev_io_channel);
    if (context->bdev_desc)
        spdk_bdev_close(context->bdev_desc);
    fprintf(stdout, "Stopping application!\n");
    spdk_app_stop(stop_rc);
}

static void read_zone_complete(struct spdk_bdev_io *bdev_io, bool success, void *arg)
{
    struct context_t *context = (struct context_t *)arg;
    spdk_bdev_free_io(bdev_io);
    
    if (success)
        fprintf(stdout, "%s", context->buf);
    else
        fprintf(stderr, "\nbdev I/O error!\n");
    
    release(context, success ? 0 : -1);
}

static void read_zone(void *arg)
{
    struct context_t *context = (struct context_t *)arg;
    
    memset(context->buf, 0, context->buf_size);
    
    fprintf(stdout, "Reading from bdev.\n");
    
    int rc = spdk_bdev_read(context->bdev_desc, context->bdev_io_channel, context->buf, zone_id, context->buf_size, read_zone_complete, context);
    if (rc == -ENOMEM) {
        fprintf(stdout, "Queueing I/O.\n");
        context->bdev_io_wait.bdev = context->bdev;
        context->bdev_io_wait.cb_fn = read_zone;
        context->bdev_io_wait.cb_arg = context;
        spdk_bdev_queue_io_wait(context->bdev, context->bdev_io_channel, &context->bdev_io_wait);
    } else if (rc) {
        fprintf(stderr, "%s error while reading to bdev: %d\n", spdk_strerror(-rc), rc);
        release(context, -1);
    }
}

static void append_zone_complete(struct spdk_bdev_io *bdev_io, bool success, void *arg)
{
    struct context_t *context = (struct context_t *)arg;
    spdk_bdev_free_io(bdev_io);
    
    if (success)
        fprintf(stdout, "bdev I/O append completed successfully\n");
    else {
        fprintf(stderr, "\nbdev I/O append error: %d\n", EIO);
        release(context, -1);
        return;
    }
    
    read_zone(context);
}

static void append_zone(void *arg)
{
    struct context_t *context = (struct context_t *)arg;
    
    fprintf(stdout, "Appending to the bdev.\n");
    int rc = spdk_bdev_zone_append(context->bdev_desc, context->bdev_io_channel, context->buf, zone_id, num_blocks, append_zone_complete, context);
    if (rc == -ENOMEM) {
        fprintf(stdout, "Queueing I/O.\n");
        context->bdev_io_wait.bdev = context->bdev;
        context->bdev_io_wait.cb_fn = append_zone;
        context->bdev_io_wait.cb_arg = context;
        spdk_bdev_queue_io_wait(context->bdev, context->bdev_io_channel, &context->bdev_io_wait);
    } else if (rc) {
        fprintf(stderr, "%s error while appending to bdev: %d\n", spdk_strerror(-rc), rc);
        release(context, -1);
    }
}

static void reset_zone_complete(struct spdk_bdev_io *bdev_io, bool success, void *arg)
{
    struct context_t *context = (struct context_t *)arg;
    spdk_bdev_free_io(bdev_io);
    
    if (success)
        fprintf(stdout, "Reset zone complete!\n");
    else {
        fprintf(stderr, "\nbdev reset zone error: %d\n", EIO);
        release(context, -1);
        return;
    }
    
    append_zone(context);
}

static void reset_zone(void *arg)
{
    struct context_t *context = (struct context_t *)arg;
    
    fprintf(stdout, "Resetting zone: %ld\n", zone_id);
    int rc = spdk_bdev_zone_management(context->bdev_desc, context->bdev_io_channel, zone_id, SPDK_BDEV_ZONE_RESET, reset_zone_complete, context);
    if (rc == -ENOMEM) {
        fprintf(stdout, "Queueing I/O.\n");
        context->bdev_io_wait.bdev = context->bdev;
        context->bdev_io_wait.cb_fn = reset_zone;
        context->bdev_io_wait.cb_arg = context;
        spdk_bdev_queue_io_wait(context->bdev, context->bdev_io_channel, &context->bdev_io_wait);
    } else if (rc) {
        fprintf(stderr, "%s error while resetting zone: %d\n", spdk_strerror(-rc), rc);
        release(context, -1);
    }
}

static void print_zbd_msg(struct context_t *arg)
{
    struct spdk_bdev *bdev = arg->bdev;
    
    fprintf(stdout, "\n=============== SPDK bdev information ===============\n\n");
    fprintf(stdout, "Block device name: %s\n", ((struct context_t *)arg)->bdev_name);
    fprintf(stdout, "nr_zones: %ld\n", spdk_bdev_get_num_zones(bdev));
    fprintf(stdout, "Zone size: %ld\n", spdk_bdev_get_zone_size(bdev));
    fprintf(stdout, "Block size: %d\n", spdk_bdev_get_block_size(bdev));
    fprintf(stdout, "Zone append limit: %d\n", spdk_bdev_get_max_zone_append_size(bdev));
    fprintf(stdout, "Open zones limit: %d\n", spdk_bdev_get_max_open_zones(bdev));
    fprintf(stdout, "Active zones limit: %d\n", spdk_bdev_get_max_active_zones(bdev));
    fprintf(stdout, "\n======================================================\n\n");
}

static void bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev, void *event_ctx)
{
    fprintf(stderr, "Unsupported bdev event: type %d\n", type);
}

static int bdev_init(void *arg)
{
    struct context_t *context = (struct context_t *)arg;
    context->bdev_name = g_bdev_name;
    context->bdev = NULL;
    context->bdev_desc = NULL;
    
    fprintf(stdout, "Opening the bdev %s.\n", context->bdev_name);
    int rc = spdk_bdev_open_ext(context->bdev_name, true, bdev_event_cb, NULL, &context->bdev_desc);
    if (rc)
        return 1;
    
    context->bdev = spdk_bdev_desc_get_bdev(context->bdev_desc);
    
    fprintf(stdout, "Opening I/O channel.\n");
    context->bdev_io_channel = spdk_bdev_get_io_channel(context->bdev_desc);
    if (!context->bdev_io_channel)
        return 2;
    
    return 0;
}

static void hello_bdev_start(void *arg)
{
    fprintf(stdout, "Successfully started the application!\n");
    
    struct context_t *context = (struct context_t *)arg;
    int rc = bdev_init(context);
    if (rc == 1) {
        fprintf(stderr, "Couldn't open bdev: %s\n", context->bdev_name);
        release(context, -1);
    }
    else if (rc == 2) {
        fprintf(stderr, "Couldn't create bdev I/O channel!\n");
        release(context, -1);
    }
    else if (rc) {
        fprintf(stderr, "Failed to initial bdev!\n");
        release(context, -1);
    }
    
    print_zbd_msg(context);
    
    context->buf_size = spdk_bdev_get_block_size(context->bdev) * spdk_bdev_get_write_unit_size(context->bdev);
    uint32_t buf_align = spdk_bdev_get_buf_align(context->bdev);
    context->buf = spdk_dma_zmalloc(context->buf_size, buf_align, NULL);
    if(!context->buf) {
        fprintf(stderr, "Failed to allocate buffer!\n");
        release(context, -1);
        return;
    }
    
    num_blocks = 1;
    snprintf(context->buf, context->buf_size, "%s", "Hello bdev!\n");
    
    zone_id = 0;
    reset_zone(context);
}

static void usage(void)
{
    fprintf(stdout, " -b <bdev>             name of the bdev to use.\n");
}

static int parse_arg(int ch, char *arg)
{
    switch (ch) {
    case 'b':
        g_bdev_name = arg;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts, sizeof(struct spdk_app_opts));
    opts.name = "hello_bdev";

    int rc = spdk_app_parse_args(argc, argv, &opts, "b:", NULL, parse_arg, usage);
    if(rc != SPDK_APP_PARSE_ARGS_SUCCESS)
        exit(rc);
    
    struct context_t context = {};    
    rc = spdk_app_start(&opts, hello_bdev_start, &context);
    if (rc)
        fprintf(stderr, "ERROR starting application!");
    
    spdk_dma_free(context.buf);
    spdk_app_fini();
    return rc;
}
