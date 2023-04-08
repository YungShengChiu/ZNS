#include <spdk/stdinc.h>
#include <spdk/nvme.h>
#include <spdk/nvme_zns.h>
#include "io_buffer.h"

#ifndef ZNS_IO_H
#define ZNS_IO_H

int zns_env_init(struct spdk_env_opts *opts, struct spdk_nvme_transport_id *trid, uint32_t nsid);

void zns_env_fini(struct spdk_nvme_ctrlr *ctrlr);

int zns_io_write(void *payload, uint64_t zslba, uint32_t lba_count);

int zns_io_read(void *payload, uint64_t lba, uint32_t lba_count);

#endif