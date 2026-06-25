#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hermes/bundle.h"
#include "hermes/api.h"
#include "hermes/crypto.h"
#include "hermes/envelope.h"
#include "hermes/identity.h"
#include "hermes/log.h"
#include "hermes/platform.h"
#include "hermes/relay.h"
#include "hermes/store.h"
#include "hermes/util.h"

static int expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test failure: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void) {
    int exit_code = 1;
    hermes_identity alice;
    hermes_identity bob;
    hermes_contact bob_contact;
    hermes_envelope env;
    hermes_envelope decoded;
    hermes_store store_a;
    hermes_store store_b;
    hermes_store store_c;
    hermes_store_stats stats;
    uint8_t encoded[HERMES_MAX_ENVELOPE_SIZE];
    size_t encoded_len = sizeof(encoded);
    uint8_t plaintext[HERMES_MAX_PLAINTEXT + 1u];
    size_t plaintext_len = HERMES_MAX_PLAINTEXT;
    const char *message = "water pump failed at station 4";
    size_t imported = 0;
    hermes_status status;
    uint64_t now = (uint64_t) hermes_now_utc();
    char bundle_path[512];
    char store_a_path[512];
    char store_b_path[512];
    char store_c_path[512];
    char relay_root[512];
    char log_dir[512];
    char bob_identity_path[512];
    char api_sender_identity_path[512];
    char api_sender_contact_path[512];
    char api_recipient_identity_path[512];
    char api_recipient_contact_path[512];
    char api_envelope_path[512];
    hermes_store_policy test_policy;

    if (!expect(hermes_platform_init() == HERMES_OK, "platform init")) goto cleanup;
    if (!expect(hermes_crypto_init() == HERMES_OK, "crypto init")) return 1;
    if (!expect(hermes_identity_generate("alice", &alice) == HERMES_OK, "alice identity")) return 1;
    if (!expect(hermes_identity_generate("bob", &bob) == HERMES_OK, "bob identity")) return 1;
    snprintf(bob_identity_path, sizeof(bob_identity_path), "work/test-bob-%lu.id", hermes_platform_process_id());
    if (!expect(hermes_identity_save(bob_identity_path, &bob) == HERMES_OK, "bob identity save")) return 1;
    if (!expect(hermes_contact_from_identity(&bob, &bob_contact) == HERMES_OK, "bob contact")) return 1;
    snprintf(store_a_path, sizeof(store_a_path), "work/test-store-a-%lu", hermes_platform_process_id());
    snprintf(store_b_path, sizeof(store_b_path), "work/test-store-b-%lu", hermes_platform_process_id());
    snprintf(store_c_path, sizeof(store_c_path), "work/test-store-c-%lu", hermes_platform_process_id());
    snprintf(relay_root, sizeof(relay_root), "work/test-relay-%lu", hermes_platform_process_id());
    snprintf(log_dir, sizeof(log_dir), "work/test-logs-%lu", hermes_platform_process_id());
    snprintf(api_sender_identity_path, sizeof(api_sender_identity_path), "work/test-api-sender-%lu.id", hermes_platform_process_id());
    snprintf(api_sender_contact_path, sizeof(api_sender_contact_path), "work/test-api-sender-%lu.contact", hermes_platform_process_id());
    snprintf(api_recipient_identity_path, sizeof(api_recipient_identity_path), "work/test-api-recipient-%lu.id", hermes_platform_process_id());
    snprintf(api_recipient_contact_path, sizeof(api_recipient_contact_path), "work/test-api-recipient-%lu.contact", hermes_platform_process_id());
    snprintf(api_envelope_path, sizeof(api_envelope_path), "work/test-api-%lu.bin", hermes_platform_process_id());
    hermes_store_default_policy(&test_policy);
    test_policy.min_pow_difficulty = 8u;
    if (!expect(hermes_store_open(&store_a, store_a_path, &test_policy) == HERMES_OK, "store a")) return 1;
    if (!expect(hermes_store_open(&store_b, store_b_path, &test_policy) == HERMES_OK, "store b")) return 1;

    status = hermes_envelope_create(&env,
                                    &alice,
                                    &bob_contact,
                                    (const uint8_t *) message,
                                    strlen(message),
                                    now,
                                    3600u,
                                    0u,
                                    8u);
    if (!expect(status == HERMES_OK, "create envelope")) return 1;
    if (!expect(hermes_envelope_encode(&env, encoded, &encoded_len) == HERMES_OK, "encode envelope")) return 1;
    if (!expect(hermes_envelope_decode(encoded, encoded_len, &decoded) == HERMES_OK, "decode envelope")) return 1;
    if (!expect(hermes_envelope_verify(&decoded, now) == HERMES_OK, "verify envelope")) return 1;
    {
        hermes_envelope variant = decoded;
        if (!expect(hermes_envelope_compute_id(&variant) == HERMES_OK, "recompute base id")) return 1;
        variant.reserved_len = 1u;
        variant.reserved[0] = 0x42u;
        if (!expect(hermes_envelope_compute_id(&variant) == HERMES_OK, "recompute variant id")) return 1;
        if (!expect(!hermes_ct_equal(variant.envelope_id, decoded.envelope_id, HERMES_ID_LEN), "reserved affects id")) return 1;
    }
    if (!expect(hermes_decrypt_payload(&bob, &decoded, decoded.encrypted_payload, decoded.payload_size, plaintext, &plaintext_len) == HERMES_OK,
                "decrypt payload")) return 1;
    plaintext[plaintext_len] = '\0';
    if (!expect(strcmp((const char *) plaintext, message) == 0, "plaintext roundtrip")) return 1;
    if (!expect(hermes_store_add_envelope(&store_a, &decoded, now) == HERMES_OK, "store add")) return 1;
    if (!expect(hermes_store_get_stats(&store_a, &stats, now) == HERMES_OK, "store stats")) return 1;
    if (!expect(stats.envelope_count == 1u, "store count")) return 1;

    snprintf(bundle_path, sizeof(bundle_path), "work/test-%lu.bundle", hermes_platform_process_id());
    if (!expect(hermes_bundle_export_store(&store_a, bundle_path, now) == HERMES_OK, "bundle export")) return 1;
    if (!expect(hermes_bundle_import_store(&store_b, bundle_path, now, &imported) == HERMES_OK, "bundle import")) return 1;
    if (!expect(imported == 1u, "bundle imported count")) return 1;
    if (!expect(hermes_store_get_stats(&store_b, &stats, now) == HERMES_OK, "store b stats")) return 1;
    if (!expect(stats.envelope_count == 1u, "store b count")) return 1;
    {
        hermes_api_identity_summary identity_summary;
        hermes_api_message_summary message_summary;
        hermes_api_message_summary verify_summary;
        hermes_api_store_stats_view api_stats;
        hermes_api_message_summary inventory[8];
        size_t inventory_count = 0;
        char plaintext_out[HERMES_MAX_PLAINTEXT + 1u];
        size_t plaintext_out_len = sizeof(plaintext_out);
        if (!expect(hermes_api_identity_generate_files("api-alice",
                                                       api_sender_identity_path,
                                                       api_sender_contact_path,
                                                       &identity_summary) == HERMES_OK,
                    "api sender identity generate")) return 1;
        if (!expect(hermes_api_identity_generate_files("api-bob",
                                                       api_recipient_identity_path,
                                                       api_recipient_contact_path,
                                                       &identity_summary) == HERMES_OK,
                    "api recipient identity generate")) return 1;
        if (!expect(identity_summary.fingerprint_hex[0] != '\0', "api fingerprint")) return 1;
        if (!expect(hermes_api_create_message(api_sender_identity_path,
                                              api_recipient_contact_path,
                                              NULL,
                                              "api path test message",
                                              3600u,
                                              8u,
                                              api_envelope_path,
                                              &message_summary) == HERMES_OK,
                    "api create message")) return 1;
        if (!expect(hermes_api_verify_envelope_file(api_envelope_path, now, &verify_summary) == HERMES_OK, "api verify envelope")) return 1;
        if (!expect(strcmp(message_summary.envelope_id_hex, verify_summary.envelope_id_hex) == 0, "api verify id match")) return 1;
        if (!expect(hermes_api_store_stats(store_a_path, now, &api_stats) == HERMES_OK, "api store stats")) return 1;
        if (!expect(api_stats.envelope_count == 1u, "api store count")) return 1;
        if (!expect(hermes_api_store_list_inventory(store_a_path, inventory, 8u, &inventory_count) == HERMES_OK, "api inventory")) return 1;
        if (!expect(inventory_count == 1u, "api inventory count")) return 1;
        if (!expect(hermes_api_store_decrypt_message(store_a_path,
                                                     bob_identity_path,
                                                     inventory[0].envelope_id_hex,
                                                     plaintext_out,
                                                     &plaintext_out_len,
                                                     NULL) == HERMES_OK,
                    "api store decrypt")) return 1;
        plaintext_out[plaintext_out_len] = '\0';
        if (!expect(strcmp(plaintext_out, message) == 0, "api plaintext roundtrip")) return 1;
    }
    {
        hermes_store_policy small_policy;
        hermes_envelope first_env;
        hermes_envelope second_env;
        hermes_envelope fetched;
        hermes_store_default_policy(&small_policy);
        small_policy.min_pow_difficulty = 8u;
        small_policy.max_envelopes = 1u;
        if (!expect(hermes_store_open(&store_c, store_c_path, &small_policy) == HERMES_OK, "store c")) return 1;
        if (!expect(hermes_envelope_create(&first_env,
                                           &alice,
                                           &bob_contact,
                                           (const uint8_t *) "first queued message",
                                           strlen("first queued message"),
                                           now,
                                           3600u,
                                           0u,
                                           8u) == HERMES_OK,
                    "first queued env")) return 1;
        if (!expect(hermes_store_add_envelope(&store_c, &first_env, now) == HERMES_OK, "store first queued")) return 1;
        if (!expect(hermes_envelope_create(&second_env,
                                           &alice,
                                           &bob_contact,
                                           (const uint8_t *) "second queued message",
                                           strlen("second queued message"),
                                           now + 1u,
                                           3600u,
                                           0u,
                                           8u) == HERMES_OK,
                    "second queued env")) return 1;
        if (!expect(hermes_store_add_envelope(&store_c, &second_env, now + 1u) == HERMES_OK, "store second queued")) return 1;
        if (!expect(hermes_store_get_envelope(&store_c, first_env.envelope_id, &fetched) == HERMES_OK, "first retained")) return 1;
        if (!expect(hermes_store_get_envelope(&store_c, second_env.envelope_id, &fetched) != HERMES_OK, "second evicted first")) return 1;
    }
    {
        hermes_logger logger;
        hermes_log_field fields[] = {
            {"message", HERMES_LOG_FIELD_STRING, "rotation-check", 0u, 0}
        };
        char log_path[512];
        char rotated_path[512];
        hermes_logger_default(&logger);
        if (!expect(hermes_mkdir_p(log_dir) == 0, "logger mkdir")) return 1;
        if (!expect(hermes_join_path(log_path, sizeof(log_path), log_dir, "relay.jsonl") == HERMES_OK, "logger path")) return 1;
        if (!expect(hermes_join_path(rotated_path, sizeof(rotated_path), log_dir, "relay.jsonl.1") == HERMES_OK, "logger rotated path")) return 1;
        memcpy(logger.path, log_path, strlen(log_path) + 1u);
        logger.rotate_bytes = 180u;
        logger.rotate_keep = 2u;
        if (!expect(hermes_logger_open(&logger) == HERMES_OK, "logger open")) return 1;
        if (!expect(hermes_logger_event(&logger, "info", "test", "event-a", fields, 1u) == HERMES_OK, "logger event a")) return 1;
        if (!expect(hermes_logger_event(&logger, "info", "test", "event-b", fields, 1u) == HERMES_OK, "logger event b")) return 1;
        if (!expect(hermes_logger_event(&logger, "info", "test", "event-c", fields, 1u) == HERMES_OK, "logger event c")) return 1;
        hermes_logger_close(&logger);
        if (!expect(hermes_path_exists(log_path), "logger current exists")) return 1;
        if (!expect(hermes_path_exists(rotated_path), "logger rotated exists")) return 1;
    }
    {
        hermes_relay_config relay_config;
        hermes_relay_config loaded_config;
        hermes_relay_node relay_node;
        hermes_relay_peer peers[8];
        size_t peer_count = 0;
        size_t processed = 0;
        hermes_store_stats relay_stats;
        char relay_in_path[512];
        hermes_relay_default_config(&relay_config);
        memcpy(relay_config.root, relay_root, strlen(relay_root) + 1u);
        memcpy(relay_config.listen_addr, "127.0.0.1:9555", 15u);
        relay_config.sync_interval_seconds = 60u;
        if (!expect(hermes_relay_init_layout(&relay_config) == HERMES_OK, "relay init layout")) return 1;
        if (!expect(hermes_relay_load_config(relay_root, &loaded_config) == HERMES_OK, "relay load config")) return 1;
        if (!expect(strcmp(loaded_config.listen_addr, "127.0.0.1:9555") == 0, "relay config listen")) return 1;
        if (!expect(loaded_config.auto_learn_peers == 1u, "relay config auto learn")) return 1;
        if (!expect(loaded_config.sync_on_change == 1u, "relay config sync on change")) return 1;
        if (!expect(loaded_config.heartbeat_interval_seconds == 30u, "relay config heartbeat")) return 1;
        if (!expect(loaded_config.log_rotate_keep == 8u, "relay config log rotate keep")) return 1;
        if (!expect(loaded_config.log_path[0] != '\0', "relay config log path")) return 1;
        if (!expect(hermes_relay_add_peer(relay_root, "127.0.0.1:9440", "seed") == HERMES_OK, "relay add peer")) return 1;
        if (!expect(hermes_relay_add_peer(relay_root, "127.0.0.1:9440", "seed-updated") == HERMES_OK, "relay add peer update")) return 1;
        if (!expect(hermes_relay_list_peers(relay_root, peers, 8u, &peer_count) == HERMES_OK, "relay list peers")) return 1;
        if (!expect(peer_count == 1u, "relay peer count")) return 1;
        if (!expect(strcmp(peers[0].address, "127.0.0.1:9440") == 0, "relay peer address")) return 1;
        if (!expect(strcmp(peers[0].alias, "seed-updated") == 0, "relay peer alias")) return 1;
        if (!expect(peers[0].learned_automatically == 0u, "relay peer manual")) return 1;
        if (!expect(hermes_relay_open(&relay_node, &loaded_config) == HERMES_OK, "relay open")) return 1;
        relay_node.store.policy.min_pow_difficulty = 8u;
        if (!expect(hermes_join_path(relay_in_path, sizeof(relay_in_path), relay_node.import_dir, "queued.bundle") == HERMES_OK, "relay import path")) return 1;
        {
            uint8_t *bundle_raw = NULL;
            size_t bundle_len = 0;
            if (!expect(hermes_read_file(bundle_path, &bundle_raw, &bundle_len) == HERMES_OK, "read exported bundle")) return 1;
            if (!expect(hermes_write_file_atomic(relay_in_path, bundle_raw, bundle_len) == HERMES_OK, "write relay import")) {
                free(bundle_raw);
                return 1;
            }
            free(bundle_raw);
        }
        if (!expect(hermes_relay_process_imports(&relay_node, &processed) == HERMES_OK, "relay process imports")) return 1;
        if (!expect(processed == 1u, "relay processed count")) return 1;
        if (!expect(hermes_store_get_stats(&relay_node.store, &relay_stats, now) == HERMES_OK, "relay stats")) return 1;
        if (!expect(relay_stats.envelope_count == 1u, "relay imported envelope count")) return 1;
        if (!expect(hermes_relay_export_latest(&relay_node) == HERMES_OK, "relay export latest")) return 1;
    }

    puts("all tests passed");
    exit_code = 0;
cleanup:
    hermes_platform_shutdown();
    return exit_code;
}
