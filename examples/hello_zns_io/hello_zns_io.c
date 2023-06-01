#include "zns_io.h"
#include <spdk/env.h>

const spdk_struct_t *spdk_struct = NULL;
uint32_t nsid = 1;
static size_t block_size = 0;
static uint64_t nr_blocks_in_zone = 0;
static uint64_t nr_zones = 0;

static void hello_world(uint64_t zslba)
{
    uint64_t z_id = zslba / nr_blocks_in_zone;
    
    printf("Reset zone\n");
    int rc = zns_reset_zone(0, true);
    if (rc) {
        printf("Failed!\n");
        return;
    }

    printf("Open zone %ld\n", z_id);
    rc = zns_open_zone(zslba, false);
    if (rc) {
        printf("Failed!\n");
        return;
    }

    char *data = zns_io_malloc(block_size, zslba);
    snprintf(data, block_size, "Hello world!\n");
    uint32_t lba_count = 1;
    
    printf("Append data to zone %ld\n", z_id);
    rc = zns_io_append(data, zslba, lba_count);
    if (rc) {
        printf("Failed!\n");
        return;
    }

    data = NULL;
    uint64_t lba = 0;

    printf("Read data from zone %ld\n", z_id);
    rc = zns_io_read(&data, lba, lba_count);
    if (rc) {
        printf("Failed!\n");
        return;
    }
    printf("%s", data);

    printf("Close zone %ld\n", z_id);
    rc = zns_close_zone(zslba, false);
    if (rc)
        printf("Failed!\n");
}

static void get_zone_info(void)
{
    block_size = zns_get_block_size();
    nr_blocks_in_zone = zns_get_nr_blocks_in_zone();
    nr_zones = zns_get_nr_zones();
}

static void print_zone_info(void)
{
    printf("\n*\tblock_size = %ld\n", block_size);
    printf("*\tnr_blocks_in_zone = %ld\n", nr_blocks_in_zone);
    printf("*\tnr_zones = %ld\n\n", nr_zones);
}

int main(void)
{
    struct spdk_env_opts opts;
    struct spdk_nvme_transport_id trid = {};
    char *env_name = "hello_zns_io";

    int rc = zns_env_init(&opts, env_name, &trid, nsid);
    if (rc) {
        printf("env initialize is failed!!!\n");
        return 0;
    }

    get_zone_info();
    print_zone_info();

    hello_world(0);

    zns_env_fini();
}