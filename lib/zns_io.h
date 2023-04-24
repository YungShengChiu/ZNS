#include <spdk/stdinc.h>
#include <spdk/nvme.h>
#include <spdk/nvme_zns.h>
#include "zns_io_buffer.h"
#include "zns_io_map.h"

/*
 *      zns_io maintains a map and multiple queues for buffering I/Os.
 *      The address to write in the ZNS will be kept in zns_io_map.
 **/

#ifndef ZNS_IO_H
#define ZNS_IO_H

extern struct spdk_env_opts *zns_env_opts;
extern struct spdk_nvme_transport_id *zns_trid;

int zns_env_init(struct spdk_env_opts *opts, struct spdk_nvme_transport_id *trid, uint32_t nsid);

void zns_env_fini(struct spdk_nvme_ctrlr *ctrlr);

int zns_io_append(void *payload, uint64_t zslba, uint32_t lba_count);

int zns_io_read(void *payload, uint64_t lba, uint32_t lba_count);

int zns_reset_zone(uint64_t zslba, bool select_all);

#endif