#include <spdk/nvme.h>
#include <spdk/nvme_zns.h>
#include <spdk/env.h>
#include "zns_zone_manage.h"

#ifndef ZNS_INTERNAL_H
#define ZNS_INTERNAL_H

typedef struct spdk_struct_t spdk_struct_t;
typedef struct zns_info_t zns_info_t;

struct spdk_struct_t {
    struct spdk_env_opts *opts;
    struct spdk_nvme_transport_id *trid;
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_nvme_qpair *qpair;
};

struct zns_info_t {
    spdk_struct_t *spdk_struct;
    uint64_t nr_zones;
    uint64_t nr_blocks_in_ns;
    uint64_t nr_blocks_in_zone;
    size_t block_size;
    uint32_t zasl;
    uint8_t pow2_block_size;
};

extern zns_info_t *zns_info;

#endif