/* The example program refers to spdk/examples/nvme/hello_world/hello_world.c */

#include "spdk/stdinc.h"
#include "spdk/vmd.h"
#include "spdk/env.h"
#include "spdk/nvme.h"
#include "spdk/nvme_zns.h"
#include "spdk/string.h"
#include "spdk/log.h"

struct ctrlr_entry {
    struct spdk_nvme_ctrlr *ctrlr;
    TAILQ_ENTRY(ctrlr_entry) link;
    char name[1024];
};

struct ns_entry {
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    TAILQ_ENTRY(ns_entry) link;
    struct spdk_nvme_qpair *qpair;
};

struct sequence {
    struct ns_entry *ns_entry;
    char *buf;
    uint32_t using_cmb_io;
    int is_completed;
};

static TAILQ_HEAD(, ctrlr_entry) g_ctrlrs = TAILQ_HEAD_INITIALIZER(g_ctrlrs);
static TAILQ_HEAD(, ns_entry) g_nss = TAILQ_HEAD_INITIALIZER(g_nss);
static struct spdk_nvme_transport_id g_trid = {};

static void register_ns(struct spdk_nvme_ctrlr *ctrlr, struct spdk_nvme_ns *ns)
{
    if (!spdk_nvme_ns_is_active(ns))
        return;
    
    struct ns_entry *ns_entry = (struct ns_entry *)malloc(sizeof(struct ns_entry));
    
    if (!ns_entry) {
        perror("ns_entry malloc");
        exit(1);
    }
    
    ns_entry->ctrlr = ctrlr;
    ns_entry->ns = ns;
    TAILQ_INSERT_TAIL(&g_nss, ns_entry, link);
    
    fprintf(stdout, "Namespace ID: %d size: %juGB\n", spdk_nvme_ns_get_id(ns_entry->ns), spdk_nvme_ns_get_size(ns_entry->ns) / 1000000000);
}

static void reset_zone_complete(void *arg, const struct spdk_nvme_cpl *cpl)
{
    struct sequence *sequence = (struct sequence *)arg;
    sequence->is_completed = 1;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(sequence->ns_entry->qpair, (struct spdk_nvme_cpl *)cpl);
        fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&cpl->status));
        fprintf(stderr, "Reset zone I/O failed, aborting run!\n");
        sequence->is_completed = 1;
        exit(1);
    }
}

static void write_complete(void *arg, const struct spdk_nvme_cpl *cpl)
{
    struct sequence *sequence = (struct sequence *)arg;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(sequence->ns_entry->qpair, (struct spdk_nvme_cpl *)cpl);
        fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&cpl->status));
        fprintf(stderr, "Write I/O failed, aborting run!\n");
        sequence->is_completed = 2;
        exit(1);
    }
    
    // Free the buffer associated with the write I/O.
    if (sequence->using_cmb_io)
        spdk_nvme_ctrlr_unmap_cmb(sequence->ns_entry->ctrlr);
    else
        spdk_free(sequence->buf);
    
    sequence->is_completed = 1;
}

static void read_complete(void *arg, const struct spdk_nvme_cpl *cpl)
{
    struct sequence *sequence = (struct sequence *)arg;
    sequence->is_completed = 1;
    
    int rc = spdk_nvme_cpl_is_error(cpl);
    if (rc) {
        spdk_nvme_qpair_print_completion(sequence->ns_entry->qpair, (struct spdk_nvme_cpl *)cpl);
        fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&cpl->status));
        fprintf(stderr, "Read I/O failed, aborting run!\n");
        sequence->is_completed = 2;
        exit(1);
    }
    
    // Print the contents of the buffer and free the buffer.
    fprintf(stdout, "%s", sequence->buf);
    spdk_free(sequence->buf);
}

static void hello_world(void)
{
    struct ns_entry *ns_entry;
    struct sequence sequence;
    int rc;
    
    TAILQ_FOREACH(ns_entry, &g_nss, link) {
        ns_entry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, NULL, 0);
        if (!ns_entry->qpair) {
            fprintf(stderr, "Controller I/O qpair allocation failed!");
            return;
        }
        
        sequence.using_cmb_io = 1;
        fprintf(stdout, "Allocate host memory buffer.\n");
        sequence.buf = spdk_zmalloc(0x1000, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
        if (!sequence.buf) {
            fprintf(stderr, "Write buffer allocation failed!\n");
            return;
        }
        if (sequence.using_cmb_io)
            fprintf(stdout, "Using controller memory buffer for I/O.\n");
        else
            fprintf(stdout, "Using host memory buffer for I/O.\n");
        sequence.is_completed = 0;
        sequence.ns_entry = ns_entry;
        
        // Reset zone
        fprintf(stdout, "Reset zone.\n");
        rc = spdk_nvme_zns_reset_zone(sequence.ns_entry->ns, sequence.ns_entry->qpair, 0, false, reset_zone_complete, &sequence);
        if (rc) {
            fprintf(stderr, "Reset zone I/O failed!\n");
            exit(1);
        }
        
        // Poll for reset completed.
        for (; !sequence.is_completed; spdk_nvme_qpair_process_completions(sequence.ns_entry->qpair, 0));
        sequence.is_completed = 0;
        
        // Store the string into buffer (HMB).
        fprintf(stdout, "Store the string into buffer.\n");
        snprintf(sequence.buf, 0x1000, "%s", "Hello world!\n");
        
        // Write the data buffer to LBA 0 of the namespace.
        fprintf(stdout, "Write the data buffer to zone.\n");
        rc = spdk_nvme_zns_zone_append(ns_entry->ns, ns_entry->qpair, sequence.buf, 0, 1, write_complete, &sequence, 0);
        if (rc) {
            fprintf(stderr, "Starting write I/O failed!\n");
            exit(1);
        }
        
        // Poll for Write completed.
        for (; !sequence.is_completed; spdk_nvme_qpair_process_completions(ns_entry->qpair, 0));
        
        // Allocate a new buffer for reading the data back from the NVMe namespace.
        fprintf(stdout, "Allocate host memory buffer.\n");
        sequence.buf = spdk_zmalloc(0x1000, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
        
        // Read the data
        fprintf(stdout, "Read the data from zone.\n");
        rc = spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair, sequence.buf, 0, 1, read_complete, (void *)&sequence, 0);
        if (rc) {
            fprintf(stderr, "Starting read I/O failed!\n");
            exit(1);
        }
        
        // Poll for Read completed.
        for (; !sequence.is_completed; spdk_nvme_qpair_process_completions(ns_entry->qpair, 0));
        
        // Free the I/O qpair.
        fprintf(stdout, "Free the I/O queues.\n");
        spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpair);
    }
}

static bool probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts)
{
    fprintf(stdout, "\nAttaching to %s\n", trid->traddr);
    return true;
}

static void attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
    struct ctrlr_entry *ctrlr_entry = (struct ctrlr_entry *)malloc(sizeof(struct ctrlr_entry));
    if (!ctrlr_entry) {
        perror("ctrlr_entry malloc");
        exit(1);
    }
    fprintf(stdout, "Attached to %s\n", trid->traddr);

    const struct spdk_nvme_ctrlr_data *ctrlr_data = spdk_nvme_ctrlr_get_data(ctrlr);
    snprintf(ctrlr_entry->name, sizeof(ctrlr_entry->name), "%-20.20s (%-20.20s)", ctrlr_data->mn, ctrlr_data->sn);
    
    ctrlr_entry->ctrlr = ctrlr;
    TAILQ_INSERT_TAIL(&g_ctrlrs, ctrlr_entry, link);
    
    struct spdk_nvme_ns *ns;
    for (int nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr); 
            nsid; nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid)) {
        ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
        if(!ns)
            continue;
        register_ns(ctrlr, ns);
    }
}

static int init_env(struct spdk_env_opts *opts)
{
    spdk_env_opts_init(opts);
    opts->name = "hello_world";
    
    spdk_nvme_trid_populate_transport(&g_trid, SPDK_NVME_TRANSPORT_PCIE);
    snprintf(g_trid.subnqn, sizeof(g_trid.subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);

    if (spdk_env_init(opts) < 0) {
        fprintf(stderr, "Unable to initialize SPDK env.\n");
        return 1;
    }

    return 0; 
}

static int init_ctrlr(void)
{
    if (spdk_nvme_probe(&g_trid, NULL, probe_cb, attach_cb, NULL)) {
        fprintf(stderr, "spdk_nvme_probe() failed.\n");
        return 1;
    }
    
    if (TAILQ_EMPTY(&g_ctrlrs)) {
        fprintf(stderr, "No NVMe controllers found.\n");
        return 1;
    }
    
    return 0;
}

static void cleanup(void)
{
    struct ns_entry *ns_entry, *tmp_ns_entry;
    TAILQ_FOREACH_SAFE(ns_entry, &g_nss, link, tmp_ns_entry) {
        TAILQ_REMOVE(&g_nss, ns_entry, link);
        free(ns_entry);
    }

    struct ctrlr_entry *ctrlr_entry, *tmp_ctrlr_entry;
    struct spdk_nvme_detach_ctx *detach_ctx = NULL;
    TAILQ_FOREACH_SAFE(ctrlr_entry, &g_ctrlrs, link, tmp_ctrlr_entry) {
        TAILQ_REMOVE(&g_ctrlrs, ctrlr_entry, link);
        spdk_nvme_detach_async(ctrlr_entry->ctrlr, &detach_ctx);
        free(ctrlr_entry);
    }
    
    if (detach_ctx)
        spdk_nvme_detach_poll(detach_ctx);
}

int main(void)
{
    struct spdk_env_opts opts;
    
    int rc = init_env(&opts);
    if (rc)
        goto exit;
    
    rc = init_ctrlr();
    if (rc)
        goto exit;
    
    fprintf(stdout, "\n=============== SPDK initialize result ===============\n\n");
    fprintf(stdout, "spdk_env_opts name: %s\n", opts.name);
    fprintf(stdout, "spdk_env_opts shm_id: %d\n", opts.shm_id);
    fprintf(stdout, "spdk_env_opts mem_size: %d\n", opts.mem_size);
    fprintf(stdout, "spdk_env_opts num_pci_addr: %ld\n", opts.num_pci_addr);
    fprintf(stdout, "spdk_nvme_transport_id trtype: %d\n", g_trid.trtype);
    fprintf(stdout, "\n======================================================\n\n");

    hello_world();
    
exit:
    cleanup();
    spdk_env_fini();
    return rc;   
}
