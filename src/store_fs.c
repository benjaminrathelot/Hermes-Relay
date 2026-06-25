#include "hermes/store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hermes/envelope.h"
#include "hermes/platform.h"
#include "hermes/util.h"

typedef struct hermes_candidate {
    char path[1024];
    uint64_t age;
    uint64_t first_seen_at_local;
    uint64_t size;
    uint32_t pow_difficulty;
    int expired;
    int over_sender_soft_cap;
    int over_recipient_soft_cap;
    int invalid;
    double score;
} hermes_candidate;

static hermes_status hermes_store_dir(char *dst, size_t dst_len, const hermes_store *store, const char *name) {
    char rel[128];
    if (snprintf(rel, sizeof(rel), "%s", name) >= (int) sizeof(rel)) {
        return HERMES_ERR_RANGE;
    }
    return hermes_join_path(dst, dst_len, store->root, rel);
}

static hermes_status hermes_store_object_path(char *dst,
                                              size_t dst_len,
                                              const hermes_store *store,
                                              const uint8_t envelope_id[HERMES_ID_LEN]) {
    char id_hex[(HERMES_ID_LEN * 2u) + 1u];
    char rel[160];
    if (hermes_hex_encode(envelope_id, HERMES_ID_LEN, id_hex, sizeof(id_hex)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    if (snprintf(rel, sizeof(rel), "objects/%s.bin", id_hex) >= (int) sizeof(rel)) {
        return HERMES_ERR_RANGE;
    }
    return hermes_join_path(dst, dst_len, store->root, rel);
}

static hermes_status hermes_store_marker_path(char *dst,
                                              size_t dst_len,
                                              const hermes_store *store,
                                              const char *subdir,
                                              const uint8_t *key,
                                              size_t key_len) {
    char key_hex[(HERMES_HASH_LEN * 2u) + 1u];
    char rel[200];
    if (key_len > HERMES_HASH_LEN) {
        return HERMES_ERR_RANGE;
    }
    if (hermes_hex_encode(key, key_len, key_hex, sizeof(key_hex)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    if (snprintf(rel, sizeof(rel), "meta/%s/%s", subdir, key_hex) >= (int) sizeof(rel)) {
        return HERMES_ERR_RANGE;
    }
    return hermes_join_path(dst, dst_len, store->root, rel);
}

static hermes_status hermes_read_timestamp_file(const char *path, uint64_t *timestamp) {
    uint8_t *raw = NULL;
    size_t len = 0;
    char buffer[64];
    char *end = NULL;
    unsigned long long parsed;
    hermes_status status;
    if (!path || !timestamp) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_read_file(path, &raw, &len);
    if (status != HERMES_OK) {
        return status;
    }
    if (len == 0 || len >= sizeof(buffer)) {
        free(raw);
        return HERMES_ERR_FORMAT;
    }
    memcpy(buffer, raw, len);
    buffer[len] = '\0';
    parsed = strtoull(buffer, &end, 10);
    free(raw);
    if (end == buffer) {
        return HERMES_ERR_FORMAT;
    }
    *timestamp = (uint64_t) parsed;
    return HERMES_OK;
}

static hermes_status hermes_write_timestamp_marker(hermes_store *store,
                                                   const char *subdir,
                                                   const uint8_t *key,
                                                   size_t key_len,
                                                   uint64_t timestamp) {
    char path[1024];
    char body[64];
    int written;
    hermes_status status = hermes_store_marker_path(path, sizeof(path), store, subdir, key, key_len);
    if (status != HERMES_OK) {
        return status;
    }
    written = snprintf(body, sizeof(body), "%llu\n", (unsigned long long) timestamp);
    if (written < 0 || (size_t) written >= sizeof(body)) {
        return HERMES_ERR_RANGE;
    }
    return hermes_write_file_atomic(path, (const uint8_t *) body, (size_t) written);
}

static hermes_status hermes_count_matches(hermes_store *store,
                                          uint64_t day,
                                          const uint8_t *sender_sig,
                                          const uint8_t *recipient_hint,
                                          uint32_t *count) {
    char objects_dir[1024];
    hermes_dir_iter dir;
    hermes_dir_entry entry;
    uint32_t total = 0;
    hermes_status status;
    status = hermes_store_dir(objects_dir, sizeof(objects_dir), store, "objects");
    if (status != HERMES_OK) {
        return status;
    }
    if (hermes_dir_open(objects_dir, &dir) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    while (hermes_dir_next(&dir, &entry) > 0) {
        char path[1024];
        uint8_t *raw = NULL;
        size_t raw_len = 0;
        hermes_envelope env;
        if (!entry.is_regular) {
            continue;
        }
        if (hermes_join_path(path, sizeof(path), objects_dir, entry.name) != HERMES_OK) {
            continue;
        }
        if (hermes_read_file(path, &raw, &raw_len) != HERMES_OK) {
            continue;
        }
        if (hermes_envelope_decode(raw, raw_len, &env) == HERMES_OK) {
            if ((env.created_at_unix / 86400u) == day &&
                (!sender_sig || hermes_ct_equal(sender_sig, env.sender_public_identity.sig_public, HERMES_SIG_PUBLIC_LEN)) &&
                (!recipient_hint || hermes_ct_equal(recipient_hint, env.recipient_hint, HERMES_RECIPIENT_HINT_LEN))) {
                ++total;
            }
        }
        free(raw);
    }
    hermes_dir_close(&dir);
    *count = total;
    return HERMES_OK;
}

static hermes_status hermes_remove_expired_objects(hermes_store *store, uint64_t now_unix) {
    char objects_dir[1024];
    hermes_dir_iter dir;
    hermes_dir_entry entry;
    hermes_status status = hermes_store_dir(objects_dir, sizeof(objects_dir), store, "objects");
    if (status != HERMES_OK) {
        return status;
    }
    if (hermes_dir_open(objects_dir, &dir) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    while (hermes_dir_next(&dir, &entry) > 0) {
        char path[1024];
        uint8_t *raw = NULL;
        size_t raw_len = 0;
        hermes_envelope env;
        if (!entry.is_regular) {
            continue;
        }
        if (hermes_join_path(path, sizeof(path), objects_dir, entry.name) != HERMES_OK) {
            continue;
        }
        if (hermes_read_file(path, &raw, &raw_len) != HERMES_OK) {
            continue;
        }
        if (hermes_envelope_decode(raw, raw_len, &env) != HERMES_OK || !hermes_store_can_forward(store, &env, now_unix)) {
            hermes_platform_remove_file(path);
        }
        free(raw);
    }
    hermes_dir_close(&dir);
    return HERMES_OK;
}

static hermes_status hermes_collect_candidates(hermes_store *store,
                                               uint64_t now_unix,
                                               hermes_candidate **out_candidates,
                                               size_t *out_count,
                                               uint64_t *out_bytes) {
    char objects_dir[1024];
    hermes_dir_iter dir;
    hermes_dir_entry entry;
    hermes_candidate *items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    uint64_t bytes = 0;
    hermes_status status = hermes_store_dir(objects_dir, sizeof(objects_dir), store, "objects");
    if (status != HERMES_OK) {
        return status;
    }
    if (hermes_dir_open(objects_dir, &dir) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    while (hermes_dir_next(&dir, &entry) > 0) {
        char path[1024];
        uint8_t *raw = NULL;
        size_t raw_len = 0;
        hermes_envelope env;
        hermes_candidate item;
        uint32_t sender_day_count = 0;
        uint32_t recipient_day_count = 0;
        uint64_t first_seen_at_local = 0;

        if (!entry.is_regular) {
            continue;
        }
        if (hermes_join_path(path, sizeof(path), objects_dir, entry.name) != HERMES_OK) {
            continue;
        }
        if (hermes_read_file(path, &raw, &raw_len) != HERMES_OK) {
            continue;
        }
        memset(&item, 0, sizeof(item));
        memcpy(item.path, path, strlen(path) + 1u);
        item.size = raw_len;
        bytes += raw_len;

        if (hermes_envelope_decode(raw, raw_len, &env) != HERMES_OK) {
            item.invalid = 1;
            item.score = -1000000.0;
        } else {
            item.expired = hermes_envelope_is_expired(&env, now_unix);
            if (hermes_store_first_seen(store, env.envelope_id, &first_seen_at_local) != HERMES_OK) {
                first_seen_at_local = env.created_at_unix;
            }
            item.first_seen_at_local = first_seen_at_local;
            item.age = now_unix > first_seen_at_local ? now_unix - first_seen_at_local : 0;
            item.pow_difficulty = env.pow_difficulty;
            if (store->policy.max_messages_per_sender_per_day > 0 &&
                hermes_count_matches(store,
                                     env.created_at_unix / 86400u,
                                     env.sender_public_identity.sig_public,
                                     NULL,
                                     &sender_day_count) == HERMES_OK &&
                sender_day_count > store->policy.max_messages_per_sender_per_day) {
                item.over_sender_soft_cap = 1;
            }
            if (store->policy.max_messages_per_recipient_per_day > 0 &&
                hermes_count_matches(store,
                                     env.created_at_unix / 86400u,
                                     NULL,
                                     env.recipient_hint,
                                     &recipient_day_count) == HERMES_OK &&
                recipient_day_count > store->policy.max_messages_per_recipient_per_day) {
                item.over_recipient_soft_cap = 1;
            }
            item.score = (double) env.pow_difficulty * 1000000.0 +
                         (double) item.age -
                         ((double) item.size * 10.0) -
                         (item.over_sender_soft_cap ? 500000.0 : 0.0) -
                         (item.over_recipient_soft_cap ? 250000.0 : 0.0) -
                         (item.expired ? 10000000.0 : 0.0);
        }
        free(raw);
        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 64u : capacity * 2u;
            hermes_candidate *grown = (hermes_candidate *) realloc(items, new_capacity * sizeof(*items));
            if (!grown) {
                free(items);
                hermes_dir_close(&dir);
                return HERMES_ERR_MEMORY;
            }
            items = grown;
            capacity = new_capacity;
        }
        items[count++] = item;
    }
    hermes_dir_close(&dir);
    *out_candidates = items;
    *out_count = count;
    *out_bytes = bytes;
    return HERMES_OK;
}

static int hermes_candidate_cmp(const void *lhs, const void *rhs) {
    const hermes_candidate *a = (const hermes_candidate *) lhs;
    const hermes_candidate *b = (const hermes_candidate *) rhs;
    if (a->invalid != b->invalid) {
        return a->invalid ? -1 : 1;
    }
    if (a->expired != b->expired) {
        return a->expired ? -1 : 1;
    }
    if (a->score < b->score) {
        return -1;
    }
    if (a->score > b->score) {
        return 1;
    }
    return 0;
}

static hermes_status hermes_cleanup_markers(const char *directory, uint64_t threshold_unix) {
    hermes_dir_iter dir;
    hermes_dir_entry entry;
    if (hermes_dir_open(directory, &dir) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    while (hermes_dir_next(&dir, &entry) > 0) {
        char path[1024];
        uint64_t timestamp = 0;
        if (!entry.is_regular) {
            continue;
        }
        if (hermes_join_path(path, sizeof(path), directory, entry.name) != HERMES_OK) {
            continue;
        }
        if (hermes_read_timestamp_file(path, &timestamp) == HERMES_OK && timestamp < threshold_unix) {
            hermes_platform_remove_file(path);
        }
    }
    hermes_dir_close(&dir);
    return HERMES_OK;
}

void hermes_store_default_policy(hermes_store_policy *policy) {
    if (!policy) {
        return;
    }
    memset(policy, 0, sizeof(*policy));
    policy->max_store_bytes = 50ull * 1024ull * 1024ull;
    policy->max_envelopes = 25000u;
    policy->max_envelope_size = HERMES_MAX_ENVELOPE_SIZE;
    policy->max_ttl_seconds = HERMES_MAX_TTL_SECONDS;
    policy->min_pow_difficulty = 28u;
    policy->max_future_skew_seconds = 12u * 60u * 60u;
    policy->max_messages_per_sender_per_day = 32u;
    policy->max_messages_per_recipient_per_day = 64u;
    policy->dedup_retention_seconds = HERMES_DEDUP_RETENTION_SECONDS;
}

hermes_status hermes_store_open(hermes_store *store, const char *root, const hermes_store_policy *policy) {
    char path[1024];
    if (!store || !root) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(store, 0, sizeof(*store));
    if (strlen(root) >= sizeof(store->root)) {
        return HERMES_ERR_RANGE;
    }
    memcpy(store->root, root, strlen(root) + 1u);
    if (policy) {
        store->policy = *policy;
    } else {
        hermes_store_default_policy(&store->policy);
    }
    if (hermes_mkdir_p(root) != 0) {
        return HERMES_ERR_IO;
    }
    if (hermes_store_dir(path, sizeof(path), store, "objects") != HERMES_OK || hermes_mkdir_p(path) != 0) {
        return HERMES_ERR_IO;
    }
    if (hermes_store_dir(path, sizeof(path), store, "meta/seen-id") != HERMES_OK || hermes_mkdir_p(path) != 0) {
        return HERMES_ERR_IO;
    }
    if (hermes_store_dir(path, sizeof(path), store, "meta/seen-hash") != HERMES_OK || hermes_mkdir_p(path) != 0) {
        return HERMES_ERR_IO;
    }
    return HERMES_OK;
}

hermes_status hermes_store_has_seen_id(hermes_store *store, const uint8_t envelope_id[HERMES_ID_LEN]) {
    char path[1024];
    hermes_status status;
    if (!store || !envelope_id) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_store_marker_path(path, sizeof(path), store, "seen-id", envelope_id, HERMES_ID_LEN);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_path_exists(path) ? HERMES_OK : HERMES_ERR_NOT_FOUND;
}

hermes_status hermes_store_has_seen_hash(hermes_store *store, const uint8_t hash[HERMES_HASH_LEN]) {
    char path[1024];
    hermes_status status;
    if (!store || !hash) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_store_marker_path(path, sizeof(path), store, "seen-hash", hash, HERMES_HASH_LEN);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_path_exists(path) ? HERMES_OK : HERMES_ERR_NOT_FOUND;
}

hermes_status hermes_store_mark_seen_id(hermes_store *store,
                                        const uint8_t envelope_id[HERMES_ID_LEN],
                                        uint64_t seen_at_unix) {
    return hermes_write_timestamp_marker(store, "seen-id", envelope_id, HERMES_ID_LEN, seen_at_unix);
}

hermes_status hermes_store_mark_seen_hash(hermes_store *store,
                                          const uint8_t hash[HERMES_HASH_LEN],
                                          uint64_t seen_at_unix) {
    return hermes_write_timestamp_marker(store, "seen-hash", hash, HERMES_HASH_LEN, seen_at_unix);
}

hermes_status hermes_store_first_seen(hermes_store *store,
                                      const uint8_t envelope_id[HERMES_ID_LEN],
                                      uint64_t *first_seen_at_unix) {
    char path[1024];
    hermes_status status;
    if (!store || !envelope_id || !first_seen_at_unix) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_store_marker_path(path, sizeof(path), store, "seen-id", envelope_id, HERMES_ID_LEN);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_read_timestamp_file(path, first_seen_at_unix);
}

int hermes_store_can_forward(hermes_store *store, const hermes_envelope *env, uint64_t now_unix) {
    uint64_t first_seen_at = 0;
    uint64_t local_deadline;
    if (!store || !env) {
        return 0;
    }
    if (hermes_store_first_seen(store, env->envelope_id, &first_seen_at) != HERMES_OK) {
        return !hermes_envelope_is_expired(env, now_unix);
    }
    local_deadline = env->expires_at_unix;
    if (first_seen_at + HERMES_MAX_TTL_SECONDS < local_deadline) {
        local_deadline = first_seen_at + HERMES_MAX_TTL_SECONDS;
    }
    return now_unix <= local_deadline;
}

hermes_status hermes_store_get_envelope(hermes_store *store,
                                        const uint8_t envelope_id[HERMES_ID_LEN],
                                        hermes_envelope *env) {
    char path[1024];
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_status status;
    if (!store || !envelope_id || !env) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_store_object_path(path, sizeof(path), store, envelope_id);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_read_file(path, &raw, &raw_len);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_envelope_decode(raw, raw_len, env);
    free(raw);
    return status;
}

hermes_status hermes_store_add_envelope(hermes_store *store, const hermes_envelope *env, uint64_t now_unix) {
    char path[1024];
    uint8_t buffer[HERMES_MAX_ENVELOPE_SIZE];
    size_t buffer_len = sizeof(buffer);
    uint8_t wire_hash[HERMES_HASH_LEN];
    uint32_t sender_count = 0;
    uint32_t recipient_count = 0;
    hermes_status status;

    if (!store || !env) {
        return HERMES_ERR_ARGUMENT;
    }
    if (env->encoded_size > store->policy.max_envelope_size || env->payload_size > HERMES_MAX_ENCRYPTED_PAYLOAD) {
        return HERMES_ERR_RANGE;
    }
    if (env->created_at_unix > now_unix + store->policy.max_future_skew_seconds) {
        return HERMES_ERR_RANGE;
    }
    if ((env->expires_at_unix - env->created_at_unix) > store->policy.max_ttl_seconds) {
        return HERMES_ERR_RANGE;
    }
    if (env->pow_difficulty < store->policy.min_pow_difficulty) {
        return HERMES_ERR_QUOTA;
    }
    status = hermes_envelope_verify(env, now_unix);
    if (status != HERMES_OK) {
        return status;
    }
    if (hermes_store_has_seen_id(store, env->envelope_id) == HERMES_OK) {
        return HERMES_ERR_DUPLICATE;
    }
    if (hermes_envelope_hash_wire(env, wire_hash) != HERMES_OK) {
        return HERMES_ERR_CRYPTO;
    }
    if (hermes_store_has_seen_hash(store, wire_hash) == HERMES_OK) {
        return HERMES_ERR_DUPLICATE;
    }
    if (store->policy.max_messages_per_sender_per_day > 0 &&
        hermes_count_matches(store,
                             env->created_at_unix / 86400u,
                             env->sender_public_identity.sig_public,
                             NULL,
                             &sender_count) == HERMES_OK &&
        sender_count >= store->policy.max_messages_per_sender_per_day) {
        return HERMES_ERR_QUOTA;
    }
    if (store->policy.max_messages_per_recipient_per_day > 0 &&
        hermes_count_matches(store,
                             env->created_at_unix / 86400u,
                             NULL,
                             env->recipient_hint,
                             &recipient_count) == HERMES_OK &&
        recipient_count >= store->policy.max_messages_per_recipient_per_day) {
        return HERMES_ERR_QUOTA;
    }
    status = hermes_envelope_encode(env, buffer, &buffer_len);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_store_object_path(path, sizeof(path), store, env->envelope_id);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_write_file_atomic(path, buffer, buffer_len);
    if (status != HERMES_OK) {
        return status;
    }
    if (hermes_store_mark_seen_id(store, env->envelope_id, now_unix) != HERMES_OK ||
        hermes_store_mark_seen_hash(store, wire_hash, now_unix) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    return hermes_store_enforce_quotas(store, now_unix);
}

hermes_status hermes_store_list_inventory(hermes_store *store,
                                          uint8_t *ids,
                                          size_t max_ids,
                                          size_t *out_ids) {
    char objects_dir[1024];
    hermes_dir_iter dir;
    hermes_dir_entry entry;
    size_t count = 0;
    hermes_status status;
    if (!store || !ids || !out_ids) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_store_dir(objects_dir, sizeof(objects_dir), store, "objects");
    if (status != HERMES_OK) {
        return status;
    }
    if (hermes_dir_open(objects_dir, &dir) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    while (hermes_dir_next(&dir, &entry) > 0) {
        size_t name_len;
        char id_hex[(HERMES_ID_LEN * 2u) + 1u];
        if (!entry.is_regular) {
            continue;
        }
        name_len = strlen(entry.name);
        if (name_len != (HERMES_ID_LEN * 2u) + 4u || strcmp(entry.name + name_len - 4u, ".bin") != 0) {
            continue;
        }
        if (count >= max_ids) {
            hermes_dir_close(&dir);
            return HERMES_ERR_RANGE;
        }
        memcpy(id_hex, entry.name, HERMES_ID_LEN * 2u);
        id_hex[HERMES_ID_LEN * 2u] = '\0';
        if (hermes_hex_decode(id_hex, ids + (count * HERMES_ID_LEN), HERMES_ID_LEN) == HERMES_OK) {
            ++count;
        }
    }
    hermes_dir_close(&dir);
    *out_ids = count;
    return HERMES_OK;
}

hermes_status hermes_store_get_stats(hermes_store *store, hermes_store_stats *stats, uint64_t now_unix) {
    hermes_candidate *candidates = NULL;
    size_t count = 0;
    size_t i;
    uint64_t bytes = 0;
    hermes_status status;
    if (!store || !stats) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(stats, 0, sizeof(*stats));
    status = hermes_collect_candidates(store, now_unix, &candidates, &count, &bytes);
    if (status != HERMES_OK) {
        return status;
    }
    stats->envelope_count = count;
    stats->total_bytes = bytes;
    for (i = 0; i < count; ++i) {
        if (candidates[i].expired) {
            ++stats->expired_count;
        }
        if (i == 0 || candidates[i].pow_difficulty < stats->weakest_pow) {
            stats->weakest_pow = candidates[i].pow_difficulty;
        }
        if (candidates[i].pow_difficulty > stats->strongest_pow) {
            stats->strongest_pow = candidates[i].pow_difficulty;
        }
    }
    free(candidates);
    return HERMES_OK;
}

hermes_status hermes_store_cleanup(hermes_store *store, uint64_t now_unix) {
    char path[1024];
    if (!store) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_remove_expired_objects(store, now_unix) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    if (hermes_store_dir(path, sizeof(path), store, "meta/seen-id") != HERMES_OK ||
        hermes_cleanup_markers(path, now_unix - store->policy.dedup_retention_seconds) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    if (hermes_store_dir(path, sizeof(path), store, "meta/seen-hash") != HERMES_OK ||
        hermes_cleanup_markers(path, now_unix - store->policy.dedup_retention_seconds) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    return HERMES_OK;
}

hermes_status hermes_store_enforce_quotas(hermes_store *store, uint64_t now_unix) {
    hermes_candidate *candidates = NULL;
    size_t count = 0;
    size_t i;
    uint64_t bytes = 0;
    uint64_t max_bytes;
    uint64_t max_envelopes;
    hermes_status status;
    if (!store) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_store_cleanup(store, now_unix) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    status = hermes_collect_candidates(store, now_unix, &candidates, &count, &bytes);
    if (status != HERMES_OK) {
        return status;
    }
    max_bytes = store->policy.max_store_bytes;
    max_envelopes = store->policy.max_envelopes;
    if (bytes <= max_bytes && count <= max_envelopes) {
        free(candidates);
        return HERMES_OK;
    }
    qsort(candidates, count, sizeof(*candidates), hermes_candidate_cmp);
    for (i = 0; i < count && (bytes > max_bytes || count > max_envelopes); ++i) {
        if (hermes_platform_remove_file(candidates[i].path) == 0) {
            if (bytes >= candidates[i].size) {
                bytes -= candidates[i].size;
            } else {
                bytes = 0;
            }
            if (count > 0) {
                --count;
            }
        }
    }
    free(candidates);
    return HERMES_OK;
}
