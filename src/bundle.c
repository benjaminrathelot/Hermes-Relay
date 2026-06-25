#include "hermes/bundle.h"

#include <stdlib.h>
#include <string.h>

#include "hermes/envelope.h"
#include "hermes/platform.h"
#include "hermes/util.h"

static hermes_status hermes_bundle_collect_store(hermes_store *store,
                                                 uint64_t now_unix,
                                                 hermes_envelope **out_envs,
                                                 size_t *out_count) {
    char objects_dir[1024];
    hermes_dir_iter dir;
    hermes_dir_entry entry;
    hermes_envelope *envs = NULL;
    size_t count = 0;
    size_t capacity = 0;
    hermes_status status;
    if (!store || !out_envs || !out_count) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_join_path(objects_dir, sizeof(objects_dir), store->root, "objects");
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
        if (hermes_envelope_decode(raw, raw_len, &env) == HERMES_OK &&
            hermes_store_can_forward(store, &env, now_unix)) {
            if (count == capacity) {
                size_t new_capacity = capacity == 0 ? 32u : capacity * 2u;
                hermes_envelope *grown = (hermes_envelope *) realloc(envs, new_capacity * sizeof(*envs));
                if (!grown) {
                    free(raw);
                    free(envs);
                    hermes_dir_close(&dir);
                    return HERMES_ERR_MEMORY;
                }
                envs = grown;
                capacity = new_capacity;
            }
            envs[count++] = env;
        }
        free(raw);
    }
    hermes_dir_close(&dir);
    *out_envs = envs;
    *out_count = count;
    return HERMES_OK;
}

hermes_status hermes_bundle_encode_envelopes(const hermes_envelope *envs,
                                             size_t env_count,
                                             uint8_t **bundle_bytes,
                                             size_t *bundle_len) {
    size_t total = 4u + 1u + 4u;
    size_t i;
    size_t off = 0;
    uint8_t *bundle;
    if ((!envs && env_count > 0) || !bundle_bytes || !bundle_len) {
        return HERMES_ERR_ARGUMENT;
    }
    for (i = 0; i < env_count; ++i) {
        size_t wire_len = 0;
        hermes_status status = hermes_envelope_encode(&envs[i], NULL, &wire_len);
        if (status != HERMES_OK) {
            return status;
        }
        total += 2u + wire_len;
    }
    bundle = (uint8_t *) malloc(total);
    if (!bundle) {
        return HERMES_ERR_MEMORY;
    }
    memcpy(bundle + off, "HRB1", 4u);
    off += 4u;
    bundle[off++] = HERMES_PROTOCOL_VERSION;
    hermes_write_be32(bundle + off, (uint32_t) env_count);
    off += 4u;
    for (i = 0; i < env_count; ++i) {
        uint8_t wire[HERMES_MAX_ENVELOPE_SIZE];
        size_t wire_len = sizeof(wire);
        hermes_status status = hermes_envelope_encode(&envs[i], wire, &wire_len);
        if (status != HERMES_OK) {
            free(bundle);
            return status;
        }
        hermes_write_be16(bundle + off, (uint16_t) wire_len);
        off += 2u;
        memcpy(bundle + off, wire, wire_len);
        off += wire_len;
    }
    *bundle_bytes = bundle;
    *bundle_len = total;
    return HERMES_OK;
}

hermes_status hermes_bundle_decode_envelopes(const uint8_t *bundle_bytes,
                                             size_t bundle_len,
                                             hermes_envelope **envs,
                                             size_t *env_count) {
    uint32_t count;
    size_t off = 0;
    size_t i;
    hermes_envelope *items;
    if (!bundle_bytes || !envs || !env_count || bundle_len < 9u) {
        return HERMES_ERR_ARGUMENT;
    }
    if (memcmp(bundle_bytes, "HRB1", 4u) != 0 || bundle_bytes[4] != HERMES_PROTOCOL_VERSION) {
        return HERMES_ERR_FORMAT;
    }
    off = 5u;
    count = hermes_read_be32(bundle_bytes + off);
    off += 4u;
    items = (hermes_envelope *) calloc(count == 0 ? 1u : count, sizeof(*items));
    if (!items) {
        return HERMES_ERR_MEMORY;
    }
    for (i = 0; i < count; ++i) {
        uint16_t wire_len;
        if (off + 2u > bundle_len) {
            free(items);
            return HERMES_ERR_FORMAT;
        }
        wire_len = hermes_read_be16(bundle_bytes + off);
        off += 2u;
        if (off + wire_len > bundle_len) {
            free(items);
            return HERMES_ERR_FORMAT;
        }
        if (hermes_envelope_decode(bundle_bytes + off, wire_len, &items[i]) != HERMES_OK) {
            free(items);
            return HERMES_ERR_FORMAT;
        }
        off += wire_len;
    }
    if (off != bundle_len) {
        free(items);
        return HERMES_ERR_FORMAT;
    }
    *envs = items;
    *env_count = count;
    return HERMES_OK;
}

hermes_status hermes_bundle_export_store(hermes_store *store, const char *path, uint64_t now_unix) {
    hermes_envelope *envs = NULL;
    size_t env_count = 0;
    uint8_t *bundle = NULL;
    size_t bundle_len = 0;
    hermes_status status;
    if (!store || !path) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_bundle_collect_store(store, now_unix, &envs, &env_count);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_bundle_encode_envelopes(envs, env_count, &bundle, &bundle_len);
    free(envs);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_write_file_atomic(path, bundle, bundle_len);
    free(bundle);
    return status;
}

hermes_status hermes_bundle_import_store(hermes_store *store, const char *path, uint64_t now_unix, size_t *imported) {
    uint8_t *bundle = NULL;
    size_t bundle_len = 0;
    hermes_envelope *envs = NULL;
    size_t env_count = 0;
    size_t accepted = 0;
    size_t i;
    hermes_status status;
    if (!store || !path || !imported) {
        return HERMES_ERR_ARGUMENT;
    }
    *imported = 0;
    status = hermes_read_file(path, &bundle, &bundle_len);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_bundle_decode_envelopes(bundle, bundle_len, &envs, &env_count);
    free(bundle);
    if (status != HERMES_OK) {
        return status;
    }
    for (i = 0; i < env_count; ++i) {
        if (hermes_store_add_envelope(store, &envs[i], now_unix) == HERMES_OK) {
            ++accepted;
        }
    }
    free(envs);
    *imported = accepted;
    return HERMES_OK;
}
