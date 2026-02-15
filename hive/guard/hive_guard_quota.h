/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HG_QUOTA_DEFAULT_BUCKETS 64U
#define HG_QUOTA_ID_MAX 128

typedef enum {
    HG_QUOTA_LAYER_USER = 0,
    HG_QUOTA_LAYER_GROUP,
    HG_QUOTA_LAYER_CLIENT_NODE,
    HG_QUOTA_LAYER_CLIENT_GROUP,
    HG_QUOTA_LAYER_SHARE,
    HG_QUOTA_LAYER_MAX
} hg_quota_layer_t;

typedef enum {
    HG_QUOTA_METRIC_BYTES = 0,
    HG_QUOTA_METRIC_FILES,
    HG_QUOTA_METRIC_MAX
} hg_quota_metric_t;

typedef enum {
    HG_QUOTA_STATUS_OK = 0,
    HG_QUOTA_STATUS_SOFT,
    HG_QUOTA_STATUS_HARD,
    HG_QUOTA_STATUS_STOP
} hg_quota_status_t;

typedef struct {
    uint64_t soft;
    uint64_t hard;
    uint64_t stop;
} hg_quota_thresholds_t;

typedef struct {
    hg_quota_thresholds_t bytes;
    hg_quota_thresholds_t files;
} hg_quota_limits_t;

typedef struct {
    hg_quota_layer_t layer;
    char identifier[HG_QUOTA_ID_MAX];
} hg_quota_subject_t;

// TODO: Instead of indexing clients by client_serial below, we should index by client_uuid. 
typedef struct {
    const char *user_sid;
    const char *group_sid;
    const char *client_serial;
    const char *client_superblock;
    const char *client_group;
    const char *share_name;
} hg_quota_context_t;

typedef struct {
    hg_quota_status_t status;
    hg_quota_layer_t layer;
    hg_quota_metric_t metric;
    char subject[HG_QUOTA_ID_MAX];
    uint64_t usage;
    uint64_t limit;
} hg_quota_violation_t;

void hg_quota_init(void);
void hg_quota_shutdown(void);

int hg_quota_set_layer(hg_quota_layer_t layer, const char *identifier, const hg_quota_limits_t *limits);
int hg_quota_remove_layer(hg_quota_layer_t layer, const char *identifier);

int hg_quota_set_user(const char *sid, const hg_quota_limits_t *limits);
int hg_quota_set_group(const char *group_sid, const hg_quota_limits_t *limits);
int hg_quota_set_client(const char *client_serial, const char *superblock, const hg_quota_limits_t *limits);
int hg_quota_set_client_group(const char *group_name, const hg_quota_limits_t *limits);
int hg_quota_set_share(const char *share_name, const hg_quota_limits_t *limits);

int hg_quota_remove_user(const char *sid);
int hg_quota_remove_group(const char *group_sid);
int hg_quota_remove_client(const char *client_serial, const char *superblock);
int hg_quota_remove_client_group(const char *group_name);
int hg_quota_remove_share(const char *share_name);

hg_quota_status_t hg_quota_check(const hg_quota_context_t *ctx,
                                 uint64_t used_bytes,
                                 uint64_t used_files,
                                 hg_quota_violation_t *violation);

const char *hg_quota_layer_name(hg_quota_layer_t layer);
const char *hg_quota_status_name(hg_quota_status_t status);
