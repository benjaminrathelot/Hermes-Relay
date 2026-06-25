#ifndef HERMES_RELAY_H
#define HERMES_RELAY_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"
#include "store.h"

typedef struct hermes_relay_config {
    char root[512];
    char listen_addr[128];
    char log_path[512];
    uint32_t sync_interval_seconds;
    uint32_t import_interval_seconds;
    uint32_t export_interval_seconds;
    uint32_t cleanup_interval_seconds;
    uint32_t heartbeat_interval_seconds;
    uint32_t auto_learn_peers;
    uint32_t sync_on_change;
    uint32_t max_peers;
    uint32_t peer_failure_threshold;
    uint32_t peer_prune_stale_seconds;
    uint64_t log_rotate_bytes;
    uint32_t log_rotate_keep;
    uint32_t log_mirror_stderr;
} hermes_relay_config;

typedef struct hermes_relay_peer {
    char address[128];
    char alias[64];
    uint64_t last_success_unix;
    uint64_t last_attempt_unix;
    uint32_t consecutive_failures;
    uint32_t learned_automatically;
    uint32_t inactive;
} hermes_relay_peer;

typedef struct hermes_relay_node {
    hermes_relay_config config;
    hermes_store store;
    char store_dir[512];
    char peers_path[512];
    char import_dir[512];
    char archive_dir[512];
    char export_dir[512];
    char contacts_dir[512];
    char logs_dir[512];
    char run_dir[512];
    char config_path[512];
    char status_path[512];
    char lock_path[512];
} hermes_relay_node;

void hermes_relay_default_config(hermes_relay_config *config);
hermes_status hermes_relay_init_layout(const hermes_relay_config *config);
hermes_status hermes_relay_load_config(const char *root, hermes_relay_config *config);
hermes_status hermes_relay_save_config(const hermes_relay_config *config);
hermes_status hermes_relay_open(hermes_relay_node *node, const hermes_relay_config *config);
hermes_status hermes_relay_add_peer(const char *root, const char *peer_addr, const char *alias);
hermes_status hermes_relay_list_peers(const char *root,
                                      hermes_relay_peer *peers,
                                      size_t max_peers,
                                      size_t *out_count);
hermes_status hermes_relay_process_imports(hermes_relay_node *node, size_t *processed);
hermes_status hermes_relay_export_latest(hermes_relay_node *node);
hermes_status hermes_relay_sync_once(hermes_relay_node *node, size_t *synced_peers);
hermes_status hermes_relay_run(hermes_relay_node *node);
hermes_status hermes_relay_read_status(const char *root, uint8_t **data, size_t *len);

#endif
