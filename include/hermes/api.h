#ifndef HERMES_API_H
#define HERMES_API_H

#include <stddef.h>
#include <stdint.h>

#include "relay.h"
#include "status.h"

#ifdef _WIN32
#  ifdef HERMES_API_BUILD
#    define HERMES_API_EXPORT __declspec(dllexport)
#  else
#    define HERMES_API_EXPORT __declspec(dllimport)
#  endif
#else
#  define HERMES_API_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hermes_api_identity_summary {
    char alias[64];
    char fingerprint_hex[(HERMES_HASH_LEN * 2u) + 1u];
    char recipient_hint_hex[(HERMES_RECIPIENT_HINT_LEN * 2u) + 1u];
} hermes_api_identity_summary;

typedef struct hermes_api_message_summary {
    char envelope_id_hex[(HERMES_ID_LEN * 2u) + 1u];
    uint64_t created_at_unix;
    uint64_t expires_at_unix;
    uint32_t pow_difficulty;
    uint32_t payload_size;
} hermes_api_message_summary;

typedef struct hermes_api_store_stats_view {
    uint64_t envelope_count;
    uint64_t total_bytes;
    uint64_t expired_count;
    uint64_t weakest_pow;
    uint64_t strongest_pow;
} hermes_api_store_stats_view;

typedef struct hermes_api_peer {
    char address[128];
    char alias[64];
    uint64_t last_success_unix;
    uint64_t last_attempt_unix;
    uint32_t consecutive_failures;
    uint32_t learned_automatically;
    uint32_t inactive;
} hermes_api_peer;

HERMES_API_EXPORT const char *hermes_api_version_string(void);
HERMES_API_EXPORT const char *hermes_api_status_string(hermes_status status);

HERMES_API_EXPORT hermes_status hermes_api_init(void);
HERMES_API_EXPORT void hermes_api_shutdown(void);

HERMES_API_EXPORT hermes_status hermes_api_identity_generate_files(const char *alias,
                                                                   const char *identity_path,
                                                                   const char *contact_path,
                                                                   hermes_api_identity_summary *summary);
HERMES_API_EXPORT hermes_status hermes_api_identity_load_summary(const char *identity_path,
                                                                 hermes_api_identity_summary *summary);
HERMES_API_EXPORT hermes_status hermes_api_contact_load_summary(const char *contact_path,
                                                                hermes_api_identity_summary *summary);

HERMES_API_EXPORT hermes_status hermes_api_create_message(const char *identity_path,
                                                          const char *contact_path,
                                                          const char *store_path,
                                                          const char *message_utf8,
                                                          uint64_t ttl_seconds,
                                                          uint32_t pow_difficulty,
                                                          const char *out_envelope_path,
                                                          hermes_api_message_summary *summary);
HERMES_API_EXPORT hermes_status hermes_api_verify_envelope_file(const char *envelope_path,
                                                                uint64_t now_unix,
                                                                hermes_api_message_summary *summary);
HERMES_API_EXPORT hermes_status hermes_api_decrypt_envelope_file(const char *identity_path,
                                                                 const char *envelope_path,
                                                                 char *plaintext_utf8,
                                                                 size_t *plaintext_len);

HERMES_API_EXPORT hermes_status hermes_api_import_bundle(const char *store_path,
                                                         const char *bundle_path,
                                                         uint64_t now_unix,
                                                         size_t *imported);
HERMES_API_EXPORT hermes_status hermes_api_export_bundle(const char *store_path,
                                                         const char *bundle_path,
                                                         uint64_t now_unix);
HERMES_API_EXPORT hermes_status hermes_api_store_stats(const char *store_path,
                                                       uint64_t now_unix,
                                                       hermes_api_store_stats_view *stats);
HERMES_API_EXPORT hermes_status hermes_api_store_list_inventory(const char *store_path,
                                                                hermes_api_message_summary *messages,
                                                                size_t max_messages,
                                                                size_t *out_messages);
HERMES_API_EXPORT hermes_status hermes_api_store_decrypt_message(const char *store_path,
                                                                 const char *identity_path,
                                                                 const char *envelope_id_hex,
                                                                 char *plaintext_utf8,
                                                                 size_t *plaintext_len,
                                                                 hermes_api_message_summary *summary);
HERMES_API_EXPORT hermes_status hermes_api_store_cleanup(const char *store_path, uint64_t now_unix);

HERMES_API_EXPORT hermes_status hermes_api_relay_init(const char *root, const char *listen_addr);
HERMES_API_EXPORT hermes_status hermes_api_relay_add_peer(const char *root,
                                                          const char *peer_addr,
                                                          const char *alias);
HERMES_API_EXPORT hermes_status hermes_api_relay_list_peers(const char *root,
                                                            hermes_api_peer *peers,
                                                            size_t max_peers,
                                                            size_t *out_count);
HERMES_API_EXPORT hermes_status hermes_api_relay_process_imports(const char *root, size_t *processed);
HERMES_API_EXPORT hermes_status hermes_api_relay_export_latest(const char *root, const char *out_path);
HERMES_API_EXPORT hermes_status hermes_api_relay_sync_once(const char *root, size_t *synced_peers);
HERMES_API_EXPORT hermes_status hermes_api_relay_read_status(const char *root,
                                                             char *json_data,
                                                             size_t *json_len);

#ifdef __cplusplus
}
#endif

#endif
