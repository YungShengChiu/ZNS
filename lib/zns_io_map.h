#include "zns_io_buffer.h"

/*
 *      The io_map is temperarily implemented as an array
 **/

#ifndef ZNS_IO_MAP_H
#define ZNS_IO_MAP_H

typedef struct io_map_desc_t io_map_desc_t;
typedef struct io_map_entry_t io_map_entry_t;

/*
 *      The identifier is to specify how to reference the data
 *      0x0 for NULL
 *      0x1 for only q_entry
 *      0x2 for only ZNS LBA
 *      0x3 for both q_entry and ZNS LBA
 *      0x11 for part of q_entry
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
    uint64_t *buf_wp;
    uint64_t *z_wp;
    uint8_t *z_state;
};

enum zone_state {
    ZONE_STATE_EMPTY = 0,
    ZONE_STATE_IMP_OPEN,
    ZONE_STATE_EXP_OPEN,
    ZONE_STATE_CLOSED,
    ZONE_STATE_FULL,
    ZONE_STATE_READONLY,
    ZONE_STATE_OFFLINE
};

extern io_map_desc_t *io_map_desc;

io_map_desc_t *io_map_new(void);

int io_map_init(uint64_t io_map_size, uint64_t zone_size, uint64_t nr_zones);

int io_map_free(void);

inline int io_map_state(uint64_t zslba)
{
    uint64_t z_id = zslba / io_map_desc->zone_size;
    return io_map_desc->z_state[z_id];
}

inline bool io_map_lba_in_buffer(uint64_t lba)
{
    return io_map_desc->io_map[lba].identifier == 1;
}

inline q_entry_t *io_map_get_q_entry(uint64_t lba)
{
    return io_map_desc->io_map[lba].q_entry;
}

inline uint64_t io_map_get_lba(uint64_t lba)
{
    return io_map_desc->io_map[lba].lba;
}

inline uint64_t io_map_get_buf_wp(uint64_t z_id)
{
    return io_map_desc->buf_wp[z_id];
}

inline uint64_t io_map_get_z_wp(uint64_t z_id)
{
    return io_map_desc->z_wp[z_id];
}

inline uint8_t io_map_get_identifier(uint64_t lba)
{
    return io_map_desc->io_map[lba].identifier;
}

int io_map_reset_zone(uint64_t zslba, bool select_all);

int io_map_imp_open_zone(uint64_t zslba);

int io_map_exp_open_zone(uint64_t zslba, bool select_all);

int io_map_close_zone(uint64_t zslba, bool select_all);

int io_map_finish_zone(uint64_t zslba, bool select_all);

int io_map_offline_zone(uint64_t zslba, bool select_all);

int io_map_append_buf(uint64_t wp, q_entry_t *q_entry, uint64_t lba_count);

int io_map_append_zns(uint64_t z_id, uint64_t wp, uint64_t lba_count);

#endif