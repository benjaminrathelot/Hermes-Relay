#include "hermes/api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hermes/bundle.h"
#include "hermes/crypto.h"
#include "hermes/envelope.h"
#include "hermes/identity.h"
#include "hermes/platform.h"
#include "hermes/store.h"
#include "hermes/util.h"

static hermes_status hermes_api_hex_summary(const hermes_contact *contact, hermes_api_identity_summary *summary) {
    if (!contact || !summary) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(summary, 0, sizeof(*summary));
    if (strlen(contact->alias) >= sizeof(summary->alias)) {
        return HERMES_ERR_RANGE;
    }
    memcpy(summary->alias, contact->alias, strlen(contact->alias) + 1u);
    if (hermes_hex_encode(contact->fingerprint,
                          HERMES_HASH_LEN,
                          summary->fingerprint_hex,
                          sizeof(summary->fingerprint_hex)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    if (hermes_hex_encode(contact->recipient_hint,
                          HERMES_RECIPIENT_HINT_LEN,
                          summary->recipient_hint_hex,
                          sizeof(summary->recipient_hint_hex)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    return HERMES_OK;
}

static hermes_status hermes_api_message_to_summary(const hermes_envelope *env, hermes_api_message_summary *summary) {
    if (!env || !summary) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(summary, 0, sizeof(*summary));
    if (hermes_hex_encode(env->envelope_id,
                          HERMES_ID_LEN,
                          summary->envelope_id_hex,
                          sizeof(summary->envelope_id_hex)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    summary->created_at_unix = env->created_at_unix;
    summary->expires_at_unix = env->expires_at_unix;
    summary->pow_difficulty = env->pow_difficulty;
    summary->payload_size = env->payload_size;
    return HERMES_OK;
}

static hermes_status hermes_api_open_store(const char *store_path, hermes_store *store) {
    hermes_store_policy policy;
    if (!store_path || !store) {
        return HERMES_ERR_ARGUMENT;
    }
    hermes_store_default_policy(&policy);
    return hermes_store_open(store, store_path, &policy);
}

static hermes_status hermes_api_decode_id_hex(const char *envelope_id_hex, uint8_t envelope_id[HERMES_ID_LEN]) {
    if (!envelope_id_hex || !envelope_id) {
        return HERMES_ERR_ARGUMENT;
    }
    return hermes_hex_decode(envelope_id_hex, envelope_id, HERMES_ID_LEN);
}

static hermes_status hermes_api_open_relay(const char *root, hermes_relay_node *node, hermes_relay_config *config) {
    hermes_status status;
    if (!root || !node || !config) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_relay_load_config(root, config);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_relay_open(node, config);
}

const char *hermes_api_version_string(void) {
    return "Hermes Relay Protocol V1";
}

const char *hermes_api_status_string(hermes_status status) {
    return hermes_status_string(status);
}

hermes_status hermes_api_init(void) {
    hermes_status status = hermes_platform_init();
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_crypto_init();
    if (status != HERMES_OK) {
        hermes_platform_shutdown();
        return status;
    }
    return HERMES_OK;
}

void hermes_api_shutdown(void) {
    hermes_crypto_cleanup();
    hermes_platform_shutdown();
}

hermes_status hermes_api_identity_generate_files(const char *alias,
                                                 const char *identity_path,
                                                 const char *contact_path,
                                                 hermes_api_identity_summary *summary) {
    hermes_identity identity;
    hermes_contact contact;
    hermes_status status;
    if (!identity_path || !contact_path || !summary) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_identity_generate(alias, &identity);
    if (status == HERMES_OK) {
        status = hermes_contact_from_identity(&identity, &contact);
    }
    if (status == HERMES_OK) {
        status = hermes_identity_save(identity_path, &identity);
    }
    if (status == HERMES_OK) {
        status = hermes_contact_save(contact_path, &contact);
    }
    if (status == HERMES_OK) {
        status = hermes_api_hex_summary(&contact, summary);
    }
    hermes_secure_bzero(&identity, sizeof(identity));
    return status;
}

hermes_status hermes_api_identity_load_summary(const char *identity_path, hermes_api_identity_summary *summary) {
    hermes_identity identity;
    hermes_contact contact;
    hermes_status status;
    if (!identity_path || !summary) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_identity_load(identity_path, &identity);
    if (status == HERMES_OK) {
        status = hermes_contact_from_identity(&identity, &contact);
    }
    if (status == HERMES_OK) {
        status = hermes_api_hex_summary(&contact, summary);
    }
    hermes_secure_bzero(&identity, sizeof(identity));
    return status;
}

hermes_status hermes_api_contact_load_summary(const char *contact_path, hermes_api_identity_summary *summary) {
    hermes_contact contact;
    hermes_status status;
    if (!contact_path || !summary) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_contact_load(contact_path, &contact);
    if (status == HERMES_OK) {
        status = hermes_api_hex_summary(&contact, summary);
    }
    return status;
}

hermes_status hermes_api_create_message(const char *identity_path,
                                        const char *contact_path,
                                        const char *store_path,
                                        const char *message_utf8,
                                        uint64_t ttl_seconds,
                                        uint32_t pow_difficulty,
                                        const char *out_envelope_path,
                                        hermes_api_message_summary *summary) {
    hermes_identity sender;
    hermes_contact recipient;
    hermes_store store;
    hermes_envelope env;
    uint64_t now_unix = (uint64_t) hermes_now_utc();
    hermes_status status;
    uint8_t wire[HERMES_MAX_ENVELOPE_SIZE];
    size_t wire_len = sizeof(wire);
    if (!identity_path || !contact_path || !message_utf8 || !summary) {
        return HERMES_ERR_ARGUMENT;
    }
    if (ttl_seconds == 0u) {
        ttl_seconds = HERMES_DEFAULT_TTL_SECONDS;
    }
    if (pow_difficulty == 0u) {
        pow_difficulty = 28u;
    }
    status = hermes_identity_load(identity_path, &sender);
    if (status == HERMES_OK) {
        status = hermes_contact_load(contact_path, &recipient);
    }
    if (status == HERMES_OK) {
        status = hermes_envelope_create(&env,
                                        &sender,
                                        &recipient,
                                        (const uint8_t *) message_utf8,
                                        strlen(message_utf8),
                                        now_unix,
                                        ttl_seconds,
                                        0u,
                                        pow_difficulty);
    }
    if (status == HERMES_OK && store_path) {
        status = hermes_api_open_store(store_path, &store);
    }
    if (status == HERMES_OK && store_path) {
        status = hermes_store_add_envelope(&store, &env, now_unix);
    }
    if (status == HERMES_OK && out_envelope_path) {
        status = hermes_envelope_encode(&env, wire, &wire_len);
        if (status == HERMES_OK) {
            status = hermes_write_file_atomic(out_envelope_path, wire, wire_len);
        }
    }
    if (status == HERMES_OK) {
        status = hermes_api_message_to_summary(&env, summary);
    }
    hermes_secure_bzero(&sender, sizeof(sender));
    return status;
}

hermes_status hermes_api_verify_envelope_file(const char *envelope_path,
                                              uint64_t now_unix,
                                              hermes_api_message_summary *summary) {
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_envelope env;
    hermes_status status;
    if (!envelope_path || !summary) {
        return HERMES_ERR_ARGUMENT;
    }
    if (now_unix == 0u) {
        now_unix = (uint64_t) hermes_now_utc();
    }
    status = hermes_read_file(envelope_path, &raw, &raw_len);
    if (status == HERMES_OK) {
        status = hermes_envelope_decode(raw, raw_len, &env);
    }
    if (status == HERMES_OK) {
        status = hermes_envelope_verify(&env, now_unix);
    }
    if (status == HERMES_OK) {
        status = hermes_api_message_to_summary(&env, summary);
    }
    free(raw);
    return status;
}

hermes_status hermes_api_decrypt_envelope_file(const char *identity_path,
                                               const char *envelope_path,
                                               char *plaintext_utf8,
                                               size_t *plaintext_len) {
    hermes_identity identity;
    hermes_contact self_contact;
    hermes_envelope env;
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_status status;
    if (!identity_path || !envelope_path || !plaintext_utf8 || !plaintext_len || *plaintext_len == 0u) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_identity_load(identity_path, &identity);
    if (status == HERMES_OK) {
        status = hermes_contact_from_identity(&identity, &self_contact);
    }
    if (status == HERMES_OK) {
        status = hermes_read_file(envelope_path, &raw, &raw_len);
    }
    if (status == HERMES_OK) {
        status = hermes_envelope_decode(raw, raw_len, &env);
    }
    if (status == HERMES_OK &&
        !hermes_ct_equal(self_contact.recipient_hint, env.recipient_hint, HERMES_RECIPIENT_HINT_LEN)) {
        status = HERMES_ERR_VERIFY;
    }
    if (status == HERMES_OK) {
        size_t buffer_len = *plaintext_len - 1u;
        status = hermes_decrypt_payload(&identity,
                                        &env,
                                        env.encrypted_payload,
                                        env.payload_size,
                                        (uint8_t *) plaintext_utf8,
                                        &buffer_len);
        if (status == HERMES_OK) {
            plaintext_utf8[buffer_len] = '\0';
            *plaintext_len = buffer_len;
        }
    }
    free(raw);
    hermes_secure_bzero(&identity, sizeof(identity));
    return status;
}

hermes_status hermes_api_import_bundle(const char *store_path,
                                       const char *bundle_path,
                                       uint64_t now_unix,
                                       size_t *imported) {
    hermes_store store;
    hermes_status status;
    if (!store_path || !bundle_path || !imported) {
        return HERMES_ERR_ARGUMENT;
    }
    if (now_unix == 0u) {
        now_unix = (uint64_t) hermes_now_utc();
    }
    status = hermes_api_open_store(store_path, &store);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_bundle_import_store(&store, bundle_path, now_unix, imported);
}

hermes_status hermes_api_export_bundle(const char *store_path, const char *bundle_path, uint64_t now_unix) {
    hermes_store store;
    hermes_status status;
    if (!store_path || !bundle_path) {
        return HERMES_ERR_ARGUMENT;
    }
    if (now_unix == 0u) {
        now_unix = (uint64_t) hermes_now_utc();
    }
    status = hermes_api_open_store(store_path, &store);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_bundle_export_store(&store, bundle_path, now_unix);
}

hermes_status hermes_api_store_stats(const char *store_path, uint64_t now_unix, hermes_api_store_stats_view *stats) {
    hermes_store store;
    hermes_store_stats native_stats;
    hermes_status status;
    if (!store_path || !stats) {
        return HERMES_ERR_ARGUMENT;
    }
    if (now_unix == 0u) {
        now_unix = (uint64_t) hermes_now_utc();
    }
    status = hermes_api_open_store(store_path, &store);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_store_get_stats(&store, &native_stats, now_unix);
    if (status != HERMES_OK) {
        return status;
    }
    memset(stats, 0, sizeof(*stats));
    stats->envelope_count = native_stats.envelope_count;
    stats->total_bytes = native_stats.total_bytes;
    stats->expired_count = native_stats.expired_count;
    stats->weakest_pow = native_stats.weakest_pow;
    stats->strongest_pow = native_stats.strongest_pow;
    return HERMES_OK;
}

hermes_status hermes_api_store_list_inventory(const char *store_path,
                                              hermes_api_message_summary *messages,
                                              size_t max_messages,
                                              size_t *out_messages) {
    hermes_store store;
    uint8_t *ids = NULL;
    size_t count = 0;
    size_t i;
    hermes_status status;
    if (!store_path || !messages || !out_messages || max_messages == 0u) {
        return HERMES_ERR_ARGUMENT;
    }
    ids = (uint8_t *) calloc(max_messages, HERMES_ID_LEN);
    if (!ids) {
        return HERMES_ERR_MEMORY;
    }
    status = hermes_api_open_store(store_path, &store);
    if (status == HERMES_OK) {
        status = hermes_store_list_inventory(&store, ids, max_messages, &count);
    }
    if (status == HERMES_OK) {
        for (i = 0; i < count; ++i) {
            hermes_envelope env;
            status = hermes_store_get_envelope(&store, ids + (i * HERMES_ID_LEN), &env);
            if (status != HERMES_OK) {
                break;
            }
            status = hermes_api_message_to_summary(&env, &messages[i]);
            if (status != HERMES_OK) {
                break;
            }
        }
    }
    free(ids);
    if (status == HERMES_OK) {
        *out_messages = count;
    }
    return status;
}

hermes_status hermes_api_store_decrypt_message(const char *store_path,
                                               const char *identity_path,
                                               const char *envelope_id_hex,
                                               char *plaintext_utf8,
                                               size_t *plaintext_len,
                                               hermes_api_message_summary *summary) {
    hermes_store store;
    hermes_identity identity;
    hermes_envelope env;
    uint8_t envelope_id[HERMES_ID_LEN];
    hermes_status status;
    if (!store_path || !identity_path || !envelope_id_hex || !plaintext_utf8 || !plaintext_len || *plaintext_len == 0u) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_api_decode_id_hex(envelope_id_hex, envelope_id);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_api_open_store(store_path, &store);
    if (status == HERMES_OK) {
        status = hermes_identity_load(identity_path, &identity);
    }
    if (status == HERMES_OK) {
        status = hermes_store_get_envelope(&store, envelope_id, &env);
    }
    if (status == HERMES_OK) {
        size_t buffer_len = *plaintext_len - 1u;
        status = hermes_decrypt_payload(&identity,
                                        &env,
                                        env.encrypted_payload,
                                        env.payload_size,
                                        (uint8_t *) plaintext_utf8,
                                        &buffer_len);
        if (status == HERMES_OK) {
            plaintext_utf8[buffer_len] = '\0';
            *plaintext_len = buffer_len;
            if (summary) {
                status = hermes_api_message_to_summary(&env, summary);
            }
        }
    }
    hermes_secure_bzero(&identity, sizeof(identity));
    return status;
}

hermes_status hermes_api_store_cleanup(const char *store_path, uint64_t now_unix) {
    hermes_store store;
    if (!store_path) {
        return HERMES_ERR_ARGUMENT;
    }
    if (now_unix == 0u) {
        now_unix = (uint64_t) hermes_now_utc();
    }
    if (hermes_api_open_store(store_path, &store) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    return hermes_store_cleanup(&store, now_unix);
}

hermes_status hermes_api_relay_init(const char *root, const char *listen_addr) {
    hermes_relay_config config;
    if (!root) {
        return HERMES_ERR_ARGUMENT;
    }
    hermes_relay_default_config(&config);
    if (strlen(root) >= sizeof(config.root)) {
        return HERMES_ERR_RANGE;
    }
    memcpy(config.root, root, strlen(root) + 1u);
    if (listen_addr) {
        if (strlen(listen_addr) >= sizeof(config.listen_addr)) {
            return HERMES_ERR_RANGE;
        }
        memcpy(config.listen_addr, listen_addr, strlen(listen_addr) + 1u);
    }
    return hermes_relay_init_layout(&config);
}

hermes_status hermes_api_relay_add_peer(const char *root, const char *peer_addr, const char *alias) {
    return hermes_relay_add_peer(root, peer_addr, alias);
}

hermes_status hermes_api_relay_list_peers(const char *root,
                                          hermes_api_peer *peers,
                                          size_t max_peers,
                                          size_t *out_count) {
    hermes_relay_peer native_peers[256];
    size_t count = 0;
    size_t i;
    hermes_status status;
    if (!root || !peers || !out_count || max_peers > 256u) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_relay_list_peers(root, native_peers, max_peers, &count);
    if (status != HERMES_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        memset(&peers[i], 0, sizeof(peers[i]));
        memcpy(peers[i].address, native_peers[i].address, strlen(native_peers[i].address) + 1u);
        memcpy(peers[i].alias, native_peers[i].alias, strlen(native_peers[i].alias) + 1u);
        peers[i].last_success_unix = native_peers[i].last_success_unix;
        peers[i].last_attempt_unix = native_peers[i].last_attempt_unix;
        peers[i].consecutive_failures = native_peers[i].consecutive_failures;
        peers[i].learned_automatically = native_peers[i].learned_automatically;
        peers[i].inactive = native_peers[i].inactive;
    }
    *out_count = count;
    return HERMES_OK;
}

hermes_status hermes_api_relay_process_imports(const char *root, size_t *processed) {
    hermes_relay_node node;
    hermes_relay_config config;
    hermes_status status;
    if (!root || !processed) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_api_open_relay(root, &node, &config);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_relay_process_imports(&node, processed);
}

hermes_status hermes_api_relay_export_latest(const char *root, const char *out_path) {
    hermes_relay_node node;
    hermes_relay_config config;
    char latest_path[1024];
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_status status;
    if (!root || !out_path) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_api_open_relay(root, &node, &config);
    if (status == HERMES_OK) {
        status = hermes_relay_export_latest(&node);
    }
    if (status == HERMES_OK) {
        status = hermes_join_path(latest_path, sizeof(latest_path), node.export_dir, "latest.bundle");
    }
    if (status == HERMES_OK) {
        status = hermes_read_file(latest_path, &raw, &raw_len);
    }
    if (status == HERMES_OK) {
        status = hermes_write_file_atomic(out_path, raw, raw_len);
    }
    free(raw);
    return status;
}

hermes_status hermes_api_relay_sync_once(const char *root, size_t *synced_peers) {
    hermes_relay_node node;
    hermes_relay_config config;
    hermes_status status;
    if (!root || !synced_peers) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_api_open_relay(root, &node, &config);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_relay_sync_once(&node, synced_peers);
}

hermes_status hermes_api_relay_read_status(const char *root, char *json_data, size_t *json_len) {
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_status status;
    if (!root || !json_data || !json_len || *json_len == 0u) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_relay_read_status(root, &raw, &raw_len);
    if (status != HERMES_OK) {
        return status;
    }
    if (raw_len + 1u > *json_len) {
        free(raw);
        return HERMES_ERR_RANGE;
    }
    memcpy(json_data, raw, raw_len);
    json_data[raw_len] = '\0';
    *json_len = raw_len;
    free(raw);
    return HERMES_OK;
}
