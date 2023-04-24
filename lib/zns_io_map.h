#include "zns_io_buffer_queue.h"

/*
 *      The io_map is temperarily implemented as an array
 **/

#ifndef ZNS_IO_MAP_H
#define ZNS_IO_MAP_H

extern io_map_desc_t *io_map_desc;

enum zone_state {
    ZONE_STATE_EMPTY = 0,
    ZONE_STATE_IMP_OPEN,
    ZONE_STATE_EXP_OPEN,
    ZONE_STATE_FULL,
    ZONE_STATE_CLOSED,
    ZONE_STATE_OFFLINE,
    ZONE_STATE_READONLY
};

typedef struct io_map_desc_t io_map_desc_t;
typedef struct io_map_entry_t io_map_entry_t;

/*
 *      The identifier is to specify how to reference the data
 *      0x0 for NULL
 *      0x1 for only q_entry
 *      0x2 for only ZNS LBA
 *      0x3 for both q_entry and ZNS LBA
 **/

struct io_map_entry_t {
    q_entry_t *q_entry;
    uint64_t lba;
    uint8_t identifier;
};

struct io_map_desc_t {
    uint64_t io_map_size;
    uint64_t zone_size;
    uint64_t nr_zones;
    io_map_entry_t *io_map;
    uint64_t *write_ptr;
    uint8_t *zone_state;
};

io_map_desc_t *io_map_new(void);

int io_map_free(void);

int io_map_init(uint64_t io_map_size, uint64_t zone_size, uint64_t nr_zones);

int io_map_reset_zone(uint64_t zslba, bool select_all);

//  TODO

#endif