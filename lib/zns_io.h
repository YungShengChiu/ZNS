#include <spdk/nvme.h>
#include <spdk/nvme_zns.h>
#include <spdk/stdinc.h>

#ifndef ZNS_IO_H
#define ZNS_IO_H

typedef void (*zns_io_update_cb)(void *cb_arg, void *payload, uint32_t lba_count);

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

#endif