#include "zns_io_buffer.h"
#include "zns_io_map.h"

#ifndef ZNS_ZONE_MANAGE_H
#define ZNS_ZONE_MANAGE_H

typedef struct zns_io_lock_t zns_io_lock_t;
typedef struct zone_management_args_t zone_management_args_t;
typedef struct zone_io_args_t zone_io_args_t;

struct zns_io_lock_t {
    pthread_mutex_t wb_lock;
    pthread_mutex_t io_buffer_lock;
    pthread_mutex_t *zone_lock;
};

struct zone_management_args_t {
    int (*cb_fn)(zone_management_args_t *);
    uint64_t zslba;
    uint64_t z_id;
    bool select_all;
};

struct zone_io_args_t {
    int (*cb_fn)(zone_io_args_t *);
    uint64_t zslba;
    uint64_t z_id;
    uint64_t lba;
    uint32_t lba_count;
};

extern zns_io_lock_t *zns_io_lock;

inline void zns_wb_lock(void)
{
    pthread_mutex_lock(&zns_io_lock->wb_lock);
}

inline void zns_wb_unlock(void)
{
    pthread_mutex_unlock(&zns_io_lock->wb_lock);
}

inline void zns_io_buf_lock(void)
{
    pthread_mutex_lock(&zns_io_lock->io_buffer_lock);
}

inline void zns_io_buf_unlock(void)
{
    pthread_mutex_unlock(&zns_io_lock->io_buffer_lock);
}

inline void zns_lock_zone(uint64_t z_id)
{
    pthread_mutex_lock(&zns_io_lock->zone_lock[z_id]);
}

inline void zns_unlock_zone(uint64_t z_id)
{
    pthread_mutex_unlock(&zns_io_lock->zone_lock[z_id]);
}

int zns_reset_zone_cb(zone_management_args_t *args);

int zns_open_zone_cb(zone_management_args_t *args);

int zns_close_zone_cb(zone_management_args_t *args);

int zns_finish_zone_cb(zone_management_args_t *args);

int zns_offline_zone_cb(zone_management_args_t *args);

int zns_append_zone_cb(zone_io_args_t *args);

int zns_read_zone_cb(zone_io_args_t *args);

#endif