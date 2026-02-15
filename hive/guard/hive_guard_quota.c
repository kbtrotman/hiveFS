/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hive_guard_quota.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



typedef struct hg_quota_node {
    uint64_t key;
    hg_quota_subject_t subject;
    hg_quota_limits_t limits;
    struct hg_quota_node *next;
} hg_quota_node_t;

static hg_quota_node_t **g_quota_buckets = NULL;
static size_t g_quota_bucket_count       = 0;
static size_t g_quota_size               = 0;
static pthread_rwlock_t g_quota_lock     = PTHREAD_RWLOCK_INITIALIZER;

static inline bool hg_quota_layer_valid(hg_quota_layer_t layer)
{
    return layer >= HG_QUOTA_LAYER_USER && layer < HG_QUOTA_LAYER_MAX;
}

static void hg_quota_subject_assign(hg_quota_subject_t *subject,
                                    hg_quota_layer_t layer,
                                    const char *identifier)
{
    if (!subject)
        return;
    subject->layer = layer;
    if (!identifier)
        identifier = "";
    strncpy(subject->identifier, identifier, HG_QUOTA_ID_MAX - 1);
    subject->identifier[HG_QUOTA_ID_MAX - 1] = '\0';
}

static bool hg_quota_subject_equal(const hg_quota_subject_t *a, const hg_quota_subject_t *b)
{
    if (!a || !b)
        return false;
    if (a->layer != b->layer)
        return false;
    return strncmp(a->identifier, b->identifier, HG_QUOTA_ID_MAX) == 0;
}

static uint64_t hg_quota_hash(const hg_quota_subject_t *subject)
{
    const unsigned char *p = (const unsigned char *)subject->identifier;
    uint64_t h = 1469598103934665603ULL;
    h ^= (uint64_t)subject->layer;
    h *= 1099511628211ULL;
    while (*p) {
        h ^= (uint64_t)*p++;
        h *= 1099511628211ULL;
    }
    return h;
}

static void hg_quota_clear_table_locked(void)
{
    if (!g_quota_buckets)
        return;

    for (size_t i = 0; i < g_quota_bucket_count; ++i) {
        hg_quota_node_t *node = g_quota_buckets[i];
        while (node) {
            hg_quota_node_t *next = node->next;
            free(node);
            node = next;
        }
    }

    free(g_quota_buckets);
    g_quota_buckets = NULL;
    g_quota_bucket_count = 0;
    g_quota_size = 0;
}

static int hg_quota_rehash_locked(size_t desired_count)
{
    size_t bucket_count = HG_QUOTA_DEFAULT_BUCKETS;
    if (desired_count > bucket_count) {
        bucket_count = 1;
        while (bucket_count < desired_count)
            bucket_count <<= 1;
    }

    hg_quota_node_t **new_table = calloc(bucket_count, sizeof(*new_table));
    if (!new_table)
        return -ENOMEM;

    if (g_quota_buckets) {
        for (size_t i = 0; i < g_quota_bucket_count; ++i) {
            hg_quota_node_t *node = g_quota_buckets[i];
            while (node) {
                hg_quota_node_t *next = node->next;
                size_t idx = node->key & (bucket_count - 1);
                node->next = new_table[idx];
                new_table[idx] = node;
                node = next;
            }
        }
        free(g_quota_buckets);
    }

    g_quota_buckets = new_table;
    g_quota_bucket_count = bucket_count;
    return 0;
}

static int hg_quota_maybe_grow_locked(void)
{
    if (g_quota_bucket_count == 0)
        return hg_quota_rehash_locked(HG_QUOTA_DEFAULT_BUCKETS);

    if (g_quota_size >= (g_quota_bucket_count * 3) / 4)
        return hg_quota_rehash_locked(g_quota_bucket_count << 1);

    return 0;
}

static hg_quota_node_t *hg_quota_lookup_locked(uint64_t key, const hg_quota_subject_t *subject)
{
    if (!g_quota_buckets || g_quota_bucket_count == 0)
        return NULL;
    size_t idx = key & (g_quota_bucket_count - 1);
    for (hg_quota_node_t *node = g_quota_buckets[idx]; node; node = node->next) {
        if (node->key == key && hg_quota_subject_equal(&node->subject, subject))
            return node;
    }
    return NULL;
}

static int hg_quota_set_locked(const hg_quota_subject_t *subject, const hg_quota_limits_t *limits)
{
    int rc = hg_quota_maybe_grow_locked();
    if (rc != 0)
        return rc;

    uint64_t key = hg_quota_hash(subject);
    size_t idx = key & (g_quota_bucket_count - 1);

    for (hg_quota_node_t *node = g_quota_buckets[idx]; node; node = node->next) {
        if (node->key == key && hg_quota_subject_equal(&node->subject, subject)) {
            node->limits = *limits;
            return 0;
        }
    }

    hg_quota_node_t *node = malloc(sizeof(*node));
    if (!node)
        return -ENOMEM;
    node->key = key;
    node->subject = *subject;
    node->limits = *limits;
    node->next = g_quota_buckets[idx];
    g_quota_buckets[idx] = node;
    ++g_quota_size;
    return 0;
}

static int hg_quota_remove_locked(const hg_quota_subject_t *subject)
{
    if (!g_quota_buckets || g_quota_bucket_count == 0)
        return -ENOENT;

    uint64_t key = hg_quota_hash(subject);
    size_t idx = key & (g_quota_bucket_count - 1);
    hg_quota_node_t **link = &g_quota_buckets[idx];

    while (*link) {
        if ((*link)->key == key && hg_quota_subject_equal(&(*link)->subject, subject)) {
            hg_quota_node_t *victim = *link;
            *link = victim->next;
            free(victim);
            --g_quota_size;
            return 0;
        }
        link = &(*link)->next;
    }

    return -ENOENT;
}

static hg_quota_status_t hg_quota_classify(uint64_t usage,
                                           const hg_quota_thresholds_t *thr,
                                           uint64_t *limit_out)
{
    if (!thr)
        return HG_QUOTA_STATUS_OK;

    if (thr->stop && usage >= thr->stop) {
        if (limit_out) *limit_out = thr->stop;
        return HG_QUOTA_STATUS_STOP;
    }
    if (thr->hard && usage >= thr->hard) {
        if (limit_out) *limit_out = thr->hard;
        return HG_QUOTA_STATUS_HARD;
    }
    if (thr->soft && usage >= thr->soft) {
        if (limit_out) *limit_out = thr->soft;
        return HG_QUOTA_STATUS_SOFT;
    }

    if (limit_out)
        *limit_out = 0;
    return HG_QUOTA_STATUS_OK;
}

static hg_quota_status_t hg_quota_eval_limits(const hg_quota_limits_t *limits,
                                              const hg_quota_subject_t *subject,
                                              uint64_t used_bytes,
                                              uint64_t used_files,
                                              hg_quota_violation_t *violation)
{
    if (!limits || !subject)
        return HG_QUOTA_STATUS_OK;

    hg_quota_status_t best = HG_QUOTA_STATUS_OK;
    hg_quota_violation_t local = {0};
    local.layer = subject->layer;
    strncpy(local.subject, subject->identifier, HG_QUOTA_ID_MAX - 1);
    local.subject[HG_QUOTA_ID_MAX - 1] = '\0';

    uint64_t limit_hit = 0;
    hg_quota_status_t st = hg_quota_classify(used_bytes, &limits->bytes, &limit_hit);
    if (st > best) {
        best = st;
        local.metric = HG_QUOTA_METRIC_BYTES;
        local.status = st;
        local.usage = used_bytes;
        local.limit = limit_hit;
    }

    st = hg_quota_classify(used_files, &limits->files, &limit_hit);
    if (st > best) {
        best = st;
        local.metric = HG_QUOTA_METRIC_FILES;
        local.status = st;
        local.usage = used_files;
        local.limit = limit_hit;
    }

    if (best > HG_QUOTA_STATUS_OK && violation)
        *violation = local;

    return best;
}

static hg_quota_status_t hg_quota_check_identifier_locked(hg_quota_layer_t layer,
                                                          const char *identifier,
                                                          uint64_t used_bytes,
                                                          uint64_t used_files,
                                                          hg_quota_violation_t *best_violation,
                                                          hg_quota_status_t current_best)
{
    if (!identifier || !*identifier || !hg_quota_layer_valid(layer))
        return current_best;

    hg_quota_subject_t subject;
    hg_quota_subject_assign(&subject, layer, identifier);

    uint64_t key = hg_quota_hash(&subject);
    hg_quota_node_t *node = hg_quota_lookup_locked(key, &subject);
    if (!node)
        return current_best;

    hg_quota_violation_t violation = {0};
    hg_quota_status_t status = hg_quota_eval_limits(&node->limits, &subject, used_bytes, used_files, &violation);
    if (status > current_best) {
        if (best_violation)
            *best_violation = violation;
        return status;
    }
    return current_best;
}

static bool hg_quota_compose_id(char *dst, size_t dst_sz, const char *primary, const char *secondary)
{
    if (!dst || dst_sz == 0 || !primary || !*primary)
        return false;

    if (secondary && *secondary)
        snprintf(dst, dst_sz, "%s@%s", primary, secondary);
    else
        strncpy(dst, primary, dst_sz - 1);

    dst[dst_sz - 1] = '\0';
    return true;
}

void hg_quota_init(void)
{
    pthread_rwlock_wrlock(&g_quota_lock);
    if (g_quota_bucket_count == 0)
        (void)hg_quota_rehash_locked(HG_QUOTA_DEFAULT_BUCKETS);
    pthread_rwlock_unlock(&g_quota_lock);
}

void hg_quota_shutdown(void)
{
    pthread_rwlock_wrlock(&g_quota_lock);
    hg_quota_clear_table_locked();
    pthread_rwlock_unlock(&g_quota_lock);
}

int hg_quota_set_layer(hg_quota_layer_t layer, const char *identifier, const hg_quota_limits_t *limits)
{
    if (!hg_quota_layer_valid(layer) || !identifier || !*identifier || !limits)
        return -EINVAL;

    hg_quota_subject_t subject;
    hg_quota_subject_assign(&subject, layer, identifier);

    pthread_rwlock_wrlock(&g_quota_lock);
    int rc = hg_quota_set_locked(&subject, limits);
    pthread_rwlock_unlock(&g_quota_lock);
    return rc;
}

int hg_quota_remove_layer(hg_quota_layer_t layer, const char *identifier)
{
    if (!hg_quota_layer_valid(layer) || !identifier || !*identifier)
        return -EINVAL;

    hg_quota_subject_t subject;
    hg_quota_subject_assign(&subject, layer, identifier);

    pthread_rwlock_wrlock(&g_quota_lock);
    int rc = hg_quota_remove_locked(&subject);
    pthread_rwlock_unlock(&g_quota_lock);
    return rc;
}

int hg_quota_set_user(const char *sid, const hg_quota_limits_t *limits)
{
    return hg_quota_set_layer(HG_QUOTA_LAYER_USER, sid, limits);
}

int hg_quota_set_group(const char *group_sid, const hg_quota_limits_t *limits)
{
    return hg_quota_set_layer(HG_QUOTA_LAYER_GROUP, group_sid, limits);
}

int hg_quota_set_client(const char *client_serial, const char *superblock, const hg_quota_limits_t *limits)
{
    char identifier[HG_QUOTA_ID_MAX];
    if (!hg_quota_compose_id(identifier, sizeof(identifier), client_serial, superblock))
        return -EINVAL;
    return hg_quota_set_layer(HG_QUOTA_LAYER_CLIENT_NODE, identifier, limits);
}

int hg_quota_set_client_group(const char *group_name, const hg_quota_limits_t *limits)
{
    return hg_quota_set_layer(HG_QUOTA_LAYER_CLIENT_GROUP, group_name, limits);
}

int hg_quota_set_share(const char *share_name, const hg_quota_limits_t *limits)
{
    return hg_quota_set_layer(HG_QUOTA_LAYER_SHARE, share_name, limits);
}

int hg_quota_remove_user(const char *sid)
{
    return hg_quota_remove_layer(HG_QUOTA_LAYER_USER, sid);
}

int hg_quota_remove_group(const char *group_sid)
{
    return hg_quota_remove_layer(HG_QUOTA_LAYER_GROUP, group_sid);
}

int hg_quota_remove_client(const char *client_serial, const char *superblock)
{
    char identifier[HG_QUOTA_ID_MAX];
    if (!hg_quota_compose_id(identifier, sizeof(identifier), client_serial, superblock))
        return -EINVAL;
    return hg_quota_remove_layer(HG_QUOTA_LAYER_CLIENT_NODE, identifier);
}

int hg_quota_remove_client_group(const char *group_name)
{
    return hg_quota_remove_layer(HG_QUOTA_LAYER_CLIENT_GROUP, group_name);
}

int hg_quota_remove_share(const char *share_name)
{
    return hg_quota_remove_layer(HG_QUOTA_LAYER_SHARE, share_name);
}

hg_quota_status_t hg_quota_check(const hg_quota_context_t *ctx,
                                 uint64_t used_bytes,
                                 uint64_t used_files,
                                 hg_quota_violation_t *violation)
{
    hg_quota_status_t best = HG_QUOTA_STATUS_OK;
    if (violation)
        memset(violation, 0, sizeof(*violation));

    if (!ctx)
        return best;

    pthread_rwlock_rdlock(&g_quota_lock);
    best = hg_quota_check_identifier_locked(HG_QUOTA_LAYER_USER, ctx->user_sid, used_bytes, used_files, violation, best);
    best = hg_quota_check_identifier_locked(HG_QUOTA_LAYER_GROUP, ctx->group_sid, used_bytes, used_files, violation, best);

    if (ctx->client_serial && *ctx->client_serial) {
        char identifier[HG_QUOTA_ID_MAX];
        hg_quota_compose_id(identifier, sizeof(identifier), ctx->client_serial, ctx->client_superblock);
        best = hg_quota_check_identifier_locked(HG_QUOTA_LAYER_CLIENT_NODE, identifier, used_bytes, used_files, violation, best);
    }

    best = hg_quota_check_identifier_locked(HG_QUOTA_LAYER_CLIENT_GROUP, ctx->client_group, used_bytes, used_files, violation, best);
    best = hg_quota_check_identifier_locked(HG_QUOTA_LAYER_SHARE, ctx->share_name, used_bytes, used_files, violation, best);
    pthread_rwlock_unlock(&g_quota_lock);

    if (best == HG_QUOTA_STATUS_OK && violation)
        memset(violation, 0, sizeof(*violation));

    return best;
}

const char *hg_quota_layer_name(hg_quota_layer_t layer)
{
    switch (layer) {
        case HG_QUOTA_LAYER_USER:         return "user";
        case HG_QUOTA_LAYER_GROUP:        return "group";
        case HG_QUOTA_LAYER_CLIENT_NODE:  return "client";
        case HG_QUOTA_LAYER_CLIENT_GROUP: return "client_group";
        case HG_QUOTA_LAYER_SHARE:        return "share";
        default:                          return "unknown";
    }
}

const char *hg_quota_status_name(hg_quota_status_t status)
{
    switch (status) {
        case HG_QUOTA_STATUS_OK:   return "ok";
        case HG_QUOTA_STATUS_SOFT: return "soft";
        case HG_QUOTA_STATUS_HARD: return "hard";
        case HG_QUOTA_STATUS_STOP: return "stop";
        default:                   return "unknown";
    }
}
