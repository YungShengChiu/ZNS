#include <spdk/nvme.h>
#include <spdk/nvme_zns.h>
#include <spdk/stdinc.h>

#ifndef ZNS_IO_H
#define ZNS_IO_H

typedef void (*zns_io_update_cb)(void *cb_arg, void *payload, uint32_t lba_count);
typedef struct spdk_struct_t spdk_struct_t;

struct spdk_struct_t {
    struct spdk_env_opts *opts;
    struct spdk_nvme_transport_id *trid;
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_nvme_qpair *qpair;
};

int zns_env_init(struct spdk_env_opts *opts, char *opts_name, struct spdk_nvme_transport_id *trid, uint32_t nsid);

void zns_env_fini(void);

int zns_reset_zone(uint64_t zslba, bool select_all);

int zns_open_zone(uint64_t zslba, bool select_all);

int zns_close_zone(uint64_t zslba, bool select_all);

int zns_finish_zone(uint64_t zslba, bool select_all);

int zns_offline_zone(uint64_t zslba, bool select_all);

int zns_io_append(void *payload, uint64_t zslba, uint32_t lba_count);

int zns_io_update(uint64_t lba, zns_io_update_cb cb_fn, void *cb_arg);

int zns_io_read(void **payload, uint64_t lba, uint32_t lba_count);

void *zns_io_malloc(size_t size, uint64_t zslba);

void zns_wait_io_complete(void);

const spdk_struct_t *zns_get_spdk_struct(void);

uint64_t zns_get_nr_zones(void);

uint64_t zns_get_nr_blocks_in_ns(void);

uint64_t zns_get_nr_blocks_in_zone(void);

size_t zns_get_block_size(void);

uint32_t zns_get_zone_append_size_limit(void);

#endif