#include <spdk/env.h>
#include "zns_zone_manage.h"
#include "zns_io.h"

#ifndef ZNS_INTERNAL_H
#define ZNS_INTERNAL_H

typedef struct zns_info_t zns_info_t;

struct zns_info_t {
    spdk_struct_t *spdk_struct;
    uint64_t nr_zones;
    uint64_t nr_blocks_in_ns;
    uint64_t nr_blocks_in_zone;
    size_t block_size;
    uint32_t zasl;
    uint32_t qd;
    uint32_t outstanding_io;
    uint8_t pow2_block_size;
};

extern zns_info_t *zns_info;

#endif