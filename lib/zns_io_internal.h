#ifndef ZNS_IO_INTERNAL_H
#define ZNS_IO_INTERNAL_H

typedef struct spdk_struct_t spdk_struct_t;

struct spdk_struct_t {
    struct spdk_env_opts *opts;
    struct spdk_nvme_transport_id *trid;
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_nvme_qpair *qpair;
};

typedef struct zone_management_args_t zone_management_args_t;

struct zone_management_args_t {
    uint64_t zslba;
    bool select_all;
    bool is_complete;
};

#endif