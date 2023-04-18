#include "zns_io_buffer_queue.h"

/*
 *      The io_map is temperarily implemented as an array
 **/

#ifndef ZNS_IO_MAP_H
#define ZNS_IO_MAP_H

extern io_map_desc_t *io_map_desc;

typedef struct io_map_desc_t io_map_desc_t;
typedef struct io_map_entry_t io_map_entry_t;

/*
 *      The identifier is to specify how to reference the data
 *      0x0 for NULL
 *      0x1 for q_entry
 *      0x2 for ZNS LBA
 **/

struct io_map_entry_t {
    union data_p {
        q_entry_t *q_entry;
        uint64_t lba;
    } data_p;
    uint8_t identifier;
};

struct io_map_desc_t {
    size_t io_map_size;
    size_t zone_size;
    io_map_entry_t *io_map;
};

io_map_desc_t *io_map_new(void);

int io_map_free(void);

int io_map_init(size_t size);

int io_map_reset_zone(uint64_t zslba);

//  TODO

#endif