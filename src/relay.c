#include "hermes/relay.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hermes/bundle.h"
#include "hermes/envelope.h"
#include "hermes/log.h"
#include "hermes/platform.h"
#include "hermes/transport.h"
#include "hermes/util.h"

#define HERMES_RELAY_CONFIG_NAME "relay.conf"
#define HERMES_RELAY_PEERS_NAME "peers.txt"
#define HERMES_RELAY_MAX_IMPORT_FILE_SIZE (64u * 1024u * 1024u)
#define HERMES_RELAY_PEER_FIELDS 7u
#define HERMES_RELAY_PEER_LINE_MAX 512u
#define HERMES_RELAY_DEFAULT_MAX_PEERS 256u
#define HERMES_RELAY_DEFAULT_PEER_FAILURE_THRESHOLD 12u
#define HERMES_RELAY_DEFAULT_PEER_PRUNE_STALE_SECONDS (30u * 24u * 60u * 60u)
#define HERMES_RELAY_DEFAULT_HEARTBEAT_INTERVAL 30u
#define HERMES_RELAY_DEFAULT_LOG_ROTATE_BYTES (16u * 1024u * 1024u)
#define HERMES_RELAY_DEFAULT_LOG_ROTATE_KEEP 8u

typedef struct hermes_relay_runtime {
    hermes_logger logger;
    hermes_platform_service_signals signals;
    uint64_t started_at_unix;
    uint64_t last_sync_at_unix;
    uint64_t last_import_at_unix;
    uint64_t last_export_at_unix;
    uint64_t last_cleanup_at_unix;
    uint64_t inbound_sessions;
    uint64_t auto_learn_events;
    uint64_t sync_failures;
    uint64_t import_failures;
    hermes_fd pid_fd;
} hermes_relay_runtime;

static void hermes_trim(char *line) {
    size_t len;
    char *start = line;
    while (*start && isspace((unsigned char) *start)) {
        ++start;
    }
    if (start != line) {
        memmove(line, start, strlen(start) + 1u);
    }
    len = strlen(line);
    while (len > 0 && isspace((unsigned char) line[len - 1u])) {
        line[--len] = '\0';
    }
}

static hermes_status hermes_relay_load_peer_file(const char *path,
                                                 hermes_relay_peer **peers,
                                                 size_t *peer_count);

static uint64_t hermes_relay_peer_reference_time(const hermes_relay_peer *peer) {
    if (!peer) {
        return 0u;
    }
    return peer->last_success_unix != 0u ? peer->last_success_unix : peer->last_attempt_unix;
}

static unsigned long hermes_relay_pid(void) {
    return hermes_platform_process_id();
}

static int hermes_relay_consume_log_reopen(hermes_relay_runtime *runtime) {
    if (runtime && runtime->signals.reopen_requested != 0) {
        runtime->signals.reopen_requested = 0;
        return 1;
    }
    return 0;
}

static int hermes_relay_process_alive(unsigned long pid) {
    return hermes_platform_process_alive(pid);
}

static hermes_status hermes_relay_acquire_pid_file(const char *path, hermes_fd *out_fd) {
    hermes_fd fd;
    char body[64];
    int written;
    if (!path || !out_fd) {
        return HERMES_ERR_ARGUMENT;
    }
    for (;;) {
        hermes_status open_status = hermes_platform_open_exclusive(path, &fd);
        if (open_status == HERMES_OK) {
            break;
        }
        if (open_status == HERMES_ERR_DUPLICATE) {
            uint8_t *raw = NULL;
            size_t raw_len = 0;
            char pid_text[64];
            char *end = NULL;
            unsigned long stale_pid = 0ul;
            if (hermes_read_file(path, &raw, &raw_len) != HERMES_OK || raw_len == 0u || raw_len >= sizeof(pid_text)) {
                if (hermes_platform_remove_file(path) != 0 && errno != ENOENT) {
                    return HERMES_ERR_IO;
                }
                continue;
            }
            memcpy(pid_text, raw, raw_len);
            pid_text[raw_len] = '\0';
            free(raw);
            errno = 0;
            stale_pid = strtoul(pid_text, &end, 10);
            if (errno != 0 || end == pid_text || (end && *end != '\n' && *end != '\0')) {
                if (hermes_platform_remove_file(path) != 0 && errno != ENOENT) {
                    return HERMES_ERR_IO;
                }
                continue;
            }
            if (!hermes_relay_process_alive(stale_pid)) {
                if (hermes_platform_remove_file(path) != 0 && errno != ENOENT) {
                    return HERMES_ERR_IO;
                }
                continue;
            }
            return HERMES_ERR_QUOTA;
        }
        return HERMES_ERR_IO;
    }
    written = snprintf(body, sizeof(body), "%lu\n", hermes_relay_pid());
    if (written < 0 || (size_t) written >= sizeof(body)) {
        hermes_platform_close_fd(fd);
        hermes_platform_remove_file(path);
        return HERMES_ERR_RANGE;
    }
    if (hermes_platform_write_fd(fd, body, (size_t) written) != written) {
        hermes_platform_close_fd(fd);
        hermes_platform_remove_file(path);
        return HERMES_ERR_IO;
    }
    *out_fd = fd;
    return HERMES_OK;
}

static void hermes_relay_release_pid_file(const char *path, hermes_fd *fd) {
    if (fd && *fd >= 0) {
        hermes_platform_close_fd(*fd);
        *fd = -1;
    }
    if (path) {
        (void) hermes_platform_remove_file(path);
    }
}

static hermes_status hermes_relay_log(hermes_relay_runtime *runtime,
                                      const char *level,
                                      const char *event,
                                      const hermes_log_field *fields,
                                      size_t field_count) {
    if (!runtime) {
        return HERMES_OK;
    }
    return hermes_logger_event(&runtime->logger, level, "relay", event, fields, field_count);
}

static int hermes_parse_u32_strict(const char *text, uint32_t *value) {
    char *end = NULL;
    unsigned long raw;
    if (!text || !value || text[0] == '\0') {
        return 0;
    }
    errno = 0;
    raw = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || raw > UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t) raw;
    return 1;
}

static int hermes_parse_u64_strict(const char *text, uint64_t *value) {
    char *end = NULL;
    unsigned long long raw;
    if (!text || !value || text[0] == '\0') {
        return 0;
    }
    errno = 0;
    raw = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        return 0;
    }
    *value = (uint64_t) raw;
    return 1;
}

static int hermes_relay_find_peer_index(const hermes_relay_peer *peers, size_t count, const char *address) {
    size_t i;
    if (!peers || !address) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(peers[i].address, address) == 0) {
            return (int) i;
        }
    }
    return -1;
}

static int hermes_relay_address_allowed(const char *value, size_t max_len) {
    size_t i;
    size_t len;
    if (!value) {
        return 0;
    }
    len = strlen(value);
    if (len == 0 || len >= max_len) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        if (value[i] == '\t' || value[i] == '\r' || value[i] == '\n') {
            return 0;
        }
    }
    return 1;
}

static int hermes_relay_alias_allowed(const char *value, size_t max_len) {
    size_t i;
    size_t len;
    if (!value) {
        return 1;
    }
    len = strlen(value);
    if (len >= max_len) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        if (value[i] == '\t' || value[i] == '\r' || value[i] == '\n') {
            return 0;
        }
    }
    return 1;
}

static hermes_status hermes_relay_parse_peer_line(char *line, hermes_relay_peer *item) {
    char *fields[HERMES_RELAY_PEER_FIELDS];
    size_t field_count = 0;
    char *cursor = line;
    if (!line || !item) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(item, 0, sizeof(*item));
    while (cursor && field_count < HERMES_RELAY_PEER_FIELDS) {
        char *tab = strchr(cursor, '\t');
        fields[field_count++] = cursor;
        if (!tab) {
            cursor = NULL;
            break;
        }
        *tab = '\0';
        cursor = tab + 1u;
    }
    if (field_count == HERMES_RELAY_PEER_FIELDS && cursor != NULL) {
        return HERMES_ERR_FORMAT;
    }
    if (field_count == HERMES_RELAY_PEER_FIELDS && cursor == NULL) {
        uint64_t last_success = 0;
        uint64_t last_attempt = 0;
        uint32_t consecutive_failures = 0;
        uint32_t learned_automatically = 0;
        uint32_t inactive = 0;
        if (!hermes_relay_address_allowed(fields[0], sizeof(item->address)) ||
            !hermes_relay_alias_allowed(fields[1], sizeof(item->alias)) ||
            !hermes_parse_u64_strict(fields[2], &last_success) ||
            !hermes_parse_u64_strict(fields[3], &last_attempt) ||
            !hermes_parse_u32_strict(fields[4], &consecutive_failures) ||
            !hermes_parse_u32_strict(fields[5], &learned_automatically) ||
            !hermes_parse_u32_strict(fields[6], &inactive)) {
            return HERMES_ERR_FORMAT;
        }
        memcpy(item->address, fields[0], strlen(fields[0]) + 1u);
        memcpy(item->alias, fields[1], strlen(fields[1]) + 1u);
        item->last_success_unix = last_success;
        item->last_attempt_unix = last_attempt;
        item->consecutive_failures = consecutive_failures;
        item->learned_automatically = learned_automatically ? 1u : 0u;
        item->inactive = inactive ? 1u : 0u;
        return HERMES_OK;
    }
    {
        char *space = strchr(line, ' ');
        if (space) {
            *space++ = '\0';
            hermes_trim(space);
            if (!hermes_relay_alias_allowed(space, sizeof(item->alias))) {
                return HERMES_ERR_RANGE;
            }
            memcpy(item->alias, space, strlen(space) + 1u);
        }
        if (!hermes_relay_address_allowed(line, sizeof(item->address))) {
            return HERMES_ERR_RANGE;
        }
        memcpy(item->address, line, strlen(line) + 1u);
        item->learned_automatically = 0u;
        item->inactive = 0u;
        item->last_attempt_unix = 0u;
        item->last_success_unix = 0u;
        item->consecutive_failures = 0u;
    }
    return HERMES_OK;
}

static hermes_status hermes_relay_save_peer_file(const char *path,
                                                 const hermes_relay_peer *peers,
                                                 size_t peer_count) {
    size_t capacity;
    size_t used = 0;
    size_t i;
    char *body;
    if (!path || (!peers && peer_count > 0)) {
        return HERMES_ERR_ARGUMENT;
    }
    if (peer_count > (SIZE_MAX / HERMES_RELAY_PEER_LINE_MAX)) {
        return HERMES_ERR_RANGE;
    }
    capacity = (peer_count == 0 ? 1u : peer_count * HERMES_RELAY_PEER_LINE_MAX);
    body = (char *) malloc(capacity);
    if (!body) {
        return HERMES_ERR_MEMORY;
    }
    for (i = 0; i < peer_count; ++i) {
        int written;
        if (!hermes_relay_address_allowed(peers[i].address, sizeof(peers[i].address)) ||
            !hermes_relay_alias_allowed(peers[i].alias, sizeof(peers[i].alias))) {
            free(body);
            return HERMES_ERR_FORMAT;
        }
        written = snprintf(body + used,
                           capacity - used,
                           "%s\t%s\t%llu\t%llu\t%u\t%u\t%u\n",
                           peers[i].address,
                           peers[i].alias,
                           (unsigned long long) peers[i].last_success_unix,
                           (unsigned long long) peers[i].last_attempt_unix,
                           peers[i].consecutive_failures,
                           peers[i].learned_automatically ? 1u : 0u,
                           peers[i].inactive ? 1u : 0u);
        if (written < 0 || (size_t) written >= capacity - used) {
            free(body);
            return HERMES_ERR_RANGE;
        }
        used += (size_t) written;
    }
    if (used == 0u) {
        hermes_status status = hermes_write_file_atomic(path, (const uint8_t *) "", 0u);
        free(body);
        return status;
    }
    {
        hermes_status status = hermes_write_file_atomic(path, (const uint8_t *) body, used);
        free(body);
        return status;
    }
}

static hermes_status hermes_relay_upsert_loaded_peer(hermes_relay_peer **items,
                                                     size_t *count,
                                                     size_t *capacity,
                                                     const hermes_relay_peer *candidate) {
    int existing;
    if (!items || !count || !capacity || !candidate) {
        return HERMES_ERR_ARGUMENT;
    }
    existing = hermes_relay_find_peer_index(*items, *count, candidate->address);
    if (existing >= 0) {
        hermes_relay_peer merged = (*items)[existing];
        if (candidate->alias[0] != '\0') {
            memcpy(merged.alias, candidate->alias, strlen(candidate->alias) + 1u);
        }
        if (candidate->last_success_unix >= merged.last_success_unix) {
            merged.last_success_unix = candidate->last_success_unix;
        }
        if (candidate->last_attempt_unix >= merged.last_attempt_unix) {
            merged.last_attempt_unix = candidate->last_attempt_unix;
        }
        if (candidate->consecutive_failures >= merged.consecutive_failures) {
            merged.consecutive_failures = candidate->consecutive_failures;
        }
        if (candidate->learned_automatically == 0u) {
            merged.learned_automatically = 0u;
        } else if (merged.learned_automatically != 0u) {
            merged.learned_automatically = 1u;
        }
        if (candidate->inactive != 0u) {
            merged.inactive = 1u;
        }
        (*items)[existing] = merged;
        return HERMES_OK;
    }
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 8u : (*capacity * 2u);
        hermes_relay_peer *grown = (hermes_relay_peer *) realloc(*items, new_capacity * sizeof(**items));
        if (!grown) {
            return HERMES_ERR_MEMORY;
        }
        *items = grown;
        *capacity = new_capacity;
    }
    (*items)[(*count)++] = *candidate;
    return HERMES_OK;
}

static hermes_status hermes_relay_upsert_peer_file(const char *path,
                                                   const hermes_relay_peer *candidate,
                                                   uint32_t max_peers) {
    hermes_relay_peer *peers = NULL;
    size_t count = 0;
    int existing;
    hermes_status status;
    if (!path || !candidate) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_relay_load_peer_file(path, &peers, &count);
    if (status != HERMES_OK) {
        return status;
    }
    existing = hermes_relay_find_peer_index(peers, count, candidate->address);
    if (existing < 0 && max_peers > 0u && count >= max_peers) {
        free(peers);
        return HERMES_ERR_QUOTA;
    }
    if (existing >= 0) {
        peers[existing] = *candidate;
    } else {
        hermes_relay_peer *grown = (hermes_relay_peer *) realloc(peers, (count + 1u) * sizeof(*peers));
        if (!grown) {
            free(peers);
            return HERMES_ERR_MEMORY;
        }
        peers = grown;
        peers[count++] = *candidate;
    }
    status = hermes_relay_save_peer_file(path, peers, count);
    free(peers);
    return status;
}

static void hermes_relay_prune_auto_peers(hermes_relay_peer *peers,
                                          size_t *count,
                                          const hermes_relay_config *config,
                                          uint64_t now_unix) {
    size_t read_idx;
    size_t write_idx = 0;
    if (!peers || !count || !config) {
        return;
    }
    for (read_idx = 0; read_idx < *count; ++read_idx) {
        uint64_t reference_time = hermes_relay_peer_reference_time(&peers[read_idx]);
        int prune = 0;
        if (peers[read_idx].learned_automatically != 0u &&
            peers[read_idx].inactive != 0u &&
            config->peer_failure_threshold > 0u &&
            peers[read_idx].consecutive_failures >= config->peer_failure_threshold &&
            config->peer_prune_stale_seconds > 0u &&
            reference_time > 0u &&
            now_unix >= reference_time &&
            now_unix - reference_time >= config->peer_prune_stale_seconds) {
            prune = 1;
        }
        if (!prune) {
            if (write_idx != read_idx) {
                peers[write_idx] = peers[read_idx];
            }
            ++write_idx;
        }
    }
    *count = write_idx;
}

static hermes_status hermes_relay_make_paths(hermes_relay_node *node) {
    if (hermes_join_path(node->store_dir, sizeof(node->store_dir), node->config.root, "store") != HERMES_OK ||
        hermes_join_path(node->import_dir, sizeof(node->import_dir), node->config.root, "import") != HERMES_OK ||
        hermes_join_path(node->archive_dir, sizeof(node->archive_dir), node->config.root, "archive") != HERMES_OK ||
        hermes_join_path(node->export_dir, sizeof(node->export_dir), node->config.root, "export") != HERMES_OK ||
        hermes_join_path(node->contacts_dir, sizeof(node->contacts_dir), node->config.root, "contacts") != HERMES_OK ||
        hermes_join_path(node->logs_dir, sizeof(node->logs_dir), node->config.root, "logs") != HERMES_OK ||
        hermes_join_path(node->run_dir, sizeof(node->run_dir), node->config.root, "run") != HERMES_OK ||
        hermes_join_path(node->peers_path, sizeof(node->peers_path), node->config.root, HERMES_RELAY_PEERS_NAME) != HERMES_OK ||
        hermes_join_path(node->config_path, sizeof(node->config_path), node->config.root, HERMES_RELAY_CONFIG_NAME) != HERMES_OK ||
        hermes_join_path(node->status_path, sizeof(node->status_path), node->run_dir, "status.json") != HERMES_OK ||
        hermes_join_path(node->lock_path, sizeof(node->lock_path), node->run_dir, "relay.pid") != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    return HERMES_OK;
}

static hermes_status hermes_relay_default_log_path(const char *root, char *dst, size_t dst_len) {
    char logs_dir[512];
    if (!root || !dst) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_join_path(logs_dir, sizeof(logs_dir), root, "logs") != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    return hermes_join_path(dst, dst_len, logs_dir, "relay.jsonl");
}

static hermes_status hermes_relay_count_peer_states(const char *peer_path,
                                                    uint64_t *peer_count,
                                                    uint64_t *active_count,
                                                    uint64_t *inactive_count) {
    hermes_relay_peer *peers = NULL;
    size_t count = 0;
    size_t i;
    hermes_status status;
    if (!peer_count || !active_count || !inactive_count) {
        return HERMES_ERR_ARGUMENT;
    }
    *peer_count = 0u;
    *active_count = 0u;
    *inactive_count = 0u;
    status = hermes_relay_load_peer_file(peer_path, &peers, &count);
    if (status != HERMES_OK) {
        return status;
    }
    *peer_count = count;
    for (i = 0; i < count; ++i) {
        if (peers[i].inactive != 0u) {
            ++(*inactive_count);
        } else {
            ++(*active_count);
        }
    }
    free(peers);
    return HERMES_OK;
}

static hermes_status hermes_relay_write_status(hermes_relay_node *node,
                                               const hermes_relay_runtime *runtime,
                                               const char *state,
                                               uint64_t now_unix) {
    hermes_store_stats stats;
    uint64_t peer_count = 0u;
    uint64_t active_peers = 0u;
    uint64_t inactive_peers = 0u;
    char body[2048];
    int written;
    if (!node || !runtime || !state) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_store_get_stats(&node->store, &stats, now_unix) != HERMES_OK) {
        memset(&stats, 0, sizeof(stats));
    }
    (void) hermes_relay_count_peer_states(node->peers_path, &peer_count, &active_peers, &inactive_peers);
    written = snprintf(body,
                       sizeof(body),
                       "{"
                       "\"state\":\"%s\","
                       "\"ts_unix\":%llu,"
                       "\"pid\":%lu,"
                       "\"listen_addr\":\"%s\","
                       "\"started_at_unix\":%llu,"
                       "\"last_sync_at_unix\":%llu,"
                       "\"last_import_at_unix\":%llu,"
                       "\"last_export_at_unix\":%llu,"
                       "\"last_cleanup_at_unix\":%llu,"
                       "\"inbound_sessions\":%llu,"
                       "\"auto_learn_events\":%llu,"
                       "\"sync_failures\":%llu,"
                       "\"import_failures\":%llu,"
                       "\"store_envelopes\":%llu,"
                       "\"store_bytes\":%llu,"
                       "\"expired_envelopes\":%llu,"
                       "\"peer_count\":%llu,"
                       "\"active_peers\":%llu,"
                       "\"inactive_peers\":%llu"
                       "}\n",
                       state,
                       (unsigned long long) now_unix,
                       hermes_relay_pid(),
                       node->config.listen_addr,
                       (unsigned long long) runtime->started_at_unix,
                       (unsigned long long) runtime->last_sync_at_unix,
                       (unsigned long long) runtime->last_import_at_unix,
                       (unsigned long long) runtime->last_export_at_unix,
                       (unsigned long long) runtime->last_cleanup_at_unix,
                       (unsigned long long) runtime->inbound_sessions,
                       (unsigned long long) runtime->auto_learn_events,
                       (unsigned long long) runtime->sync_failures,
                       (unsigned long long) runtime->import_failures,
                       (unsigned long long) stats.envelope_count,
                       (unsigned long long) stats.total_bytes,
                       (unsigned long long) stats.expired_count,
                       (unsigned long long) peer_count,
                       (unsigned long long) active_peers,
                       (unsigned long long) inactive_peers);
    if (written < 0 || (size_t) written >= sizeof(body)) {
        return HERMES_ERR_RANGE;
    }
    return hermes_write_file_atomic(node->status_path, (const uint8_t *) body, (size_t) written);
}

static hermes_status hermes_relay_runtime_open(hermes_relay_node *node, hermes_relay_runtime *runtime) {
    hermes_status status;
    if (!node || !runtime) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->pid_fd = -1;
    runtime->started_at_unix = (uint64_t) hermes_now_utc();
    if (hermes_platform_install_service_signals(&runtime->signals) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    hermes_logger_default(&runtime->logger);
    runtime->logger.rotate_bytes = node->config.log_rotate_bytes;
    runtime->logger.rotate_keep = node->config.log_rotate_keep;
    runtime->logger.mirror_stderr = node->config.log_mirror_stderr;
    if (strlen(node->config.log_path) >= sizeof(runtime->logger.path)) {
        return HERMES_ERR_RANGE;
    }
    memcpy(runtime->logger.path, node->config.log_path, strlen(node->config.log_path) + 1u);
    status = hermes_relay_acquire_pid_file(node->lock_path, &runtime->pid_fd);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_logger_open(&runtime->logger);
    if (status != HERMES_OK) {
        hermes_relay_release_pid_file(node->lock_path, &runtime->pid_fd);
        return status;
    }
    return HERMES_OK;
}

static void hermes_relay_runtime_close(hermes_relay_node *node, hermes_relay_runtime *runtime) {
    if (!node || !runtime) {
        return;
    }
    hermes_logger_close(&runtime->logger);
    hermes_relay_release_pid_file(node->lock_path, &runtime->pid_fd);
}

static hermes_status hermes_relay_import_single_envelope(hermes_relay_node *node,
                                                         const char *path,
                                                         uint64_t now_unix,
                                                         size_t *accepted) {
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_envelope env;
    hermes_status status;
    if (!node || !path || !accepted) {
        return HERMES_ERR_ARGUMENT;
    }
    *accepted = 0;
    status = hermes_read_file(path, &raw, &raw_len);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_envelope_decode(raw, raw_len, &env);
    free(raw);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_store_add_envelope(&node->store, &env, now_unix);
    if (status == HERMES_OK) {
        *accepted = 1;
    }
    return status;
}

static hermes_status hermes_relay_archive_path(hermes_relay_node *node, const char *filename, char *out, size_t out_len) {
    char rel[256];
    if (snprintf(rel, sizeof(rel), "%s.done", filename) >= (int) sizeof(rel)) {
        return HERMES_ERR_RANGE;
    }
    return hermes_join_path(out, out_len, node->archive_dir, rel);
}

static hermes_status hermes_relay_check_regular_file_limit(const char *path, uint64_t limit_bytes) {
    struct stat st;
    if (!path) {
        return HERMES_ERR_ARGUMENT;
    }
    if (stat(path, &st) != 0) {
        return HERMES_ERR_IO;
    }
    if (!S_ISREG(st.st_mode)) {
        return HERMES_ERR_FORMAT;
    }
    if ((uint64_t) st.st_size > limit_bytes) {
        return HERMES_ERR_RANGE;
    }
    return HERMES_OK;
}

static hermes_status hermes_relay_load_peer_file(const char *path,
                                                 hermes_relay_peer **peers,
                                                 size_t *peer_count) {
    FILE *fp;
    char line[256];
    hermes_relay_peer *items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!path || !peers || !peer_count) {
        return HERMES_ERR_ARGUMENT;
    }
    *peers = NULL;
    *peer_count = 0;
    fp = fopen(path, "rb");
    if (!fp) {
        return errno == ENOENT ? HERMES_OK : HERMES_ERR_IO;
    }
    while (fgets(line, sizeof(line), fp)) {
        hermes_relay_peer item;
        hermes_status parse_status;
        if (!strchr(line, '\n') && !feof(fp)) {
            fclose(fp);
            free(items);
            return HERMES_ERR_RANGE;
        }
        hermes_trim(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        parse_status = hermes_relay_parse_peer_line(line, &item);
        if (parse_status != HERMES_OK) {
            fclose(fp);
            free(items);
            return parse_status;
        }
        parse_status = hermes_relay_upsert_loaded_peer(&items, &count, &capacity, &item);
        if (parse_status != HERMES_OK) {
            fclose(fp);
            free(items);
            return parse_status;
        }
    }
    fclose(fp);
    *peers = items;
    *peer_count = count;
    return HERMES_OK;
}

void hermes_relay_default_config(hermes_relay_config *config) {
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    memcpy(config->listen_addr, "0.0.0.0:9440", 13u);
    config->sync_interval_seconds = 300u;
    config->import_interval_seconds = 15u;
    config->export_interval_seconds = 300u;
    config->cleanup_interval_seconds = 300u;
    config->heartbeat_interval_seconds = HERMES_RELAY_DEFAULT_HEARTBEAT_INTERVAL;
    config->auto_learn_peers = 1u;
    config->sync_on_change = 1u;
    config->max_peers = HERMES_RELAY_DEFAULT_MAX_PEERS;
    config->peer_failure_threshold = HERMES_RELAY_DEFAULT_PEER_FAILURE_THRESHOLD;
    config->peer_prune_stale_seconds = HERMES_RELAY_DEFAULT_PEER_PRUNE_STALE_SECONDS;
    config->log_rotate_bytes = HERMES_RELAY_DEFAULT_LOG_ROTATE_BYTES;
    config->log_rotate_keep = HERMES_RELAY_DEFAULT_LOG_ROTATE_KEEP;
    config->log_mirror_stderr = 0u;
}

hermes_status hermes_relay_save_config(const hermes_relay_config *config) {
    char path[1024];
    char body[2048];
    int written;
    if (!config || config->root[0] == '\0') {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_join_path(path, sizeof(path), config->root, HERMES_RELAY_CONFIG_NAME) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    written = snprintf(body,
                       sizeof(body),
                       "listen_addr=%s\n"
                       "sync_interval_seconds=%u\n"
                       "import_interval_seconds=%u\n"
                       "export_interval_seconds=%u\n"
                       "cleanup_interval_seconds=%u\n"
                       "heartbeat_interval_seconds=%u\n"
                       "log_path=%s\n"
                       "log_rotate_bytes=%llu\n"
                       "log_rotate_keep=%u\n"
                       "log_mirror_stderr=%u\n"
                       "auto_learn_peers=%u\n"
                       "sync_on_change=%u\n"
                       "max_peers=%u\n"
                       "peer_failure_threshold=%u\n"
                       "peer_prune_stale_seconds=%u\n",
                       config->listen_addr,
                       config->sync_interval_seconds,
                       config->import_interval_seconds,
                       config->export_interval_seconds,
                       config->cleanup_interval_seconds,
                       config->heartbeat_interval_seconds,
                       config->log_path,
                       (unsigned long long) config->log_rotate_bytes,
                       config->log_rotate_keep,
                       config->log_mirror_stderr,
                       config->auto_learn_peers,
                       config->sync_on_change,
                       config->max_peers,
                       config->peer_failure_threshold,
                       config->peer_prune_stale_seconds);
    if (written < 0 || (size_t) written >= sizeof(body)) {
        return HERMES_ERR_RANGE;
    }
    return hermes_write_file_atomic(path, (const uint8_t *) body, (size_t) written);
}

hermes_status hermes_relay_init_layout(const hermes_relay_config *config) {
    hermes_relay_node node;
    uint8_t empty_file = '\0';
    if (!config || config->root[0] == '\0') {
        return HERMES_ERR_ARGUMENT;
    }
    memset(&node, 0, sizeof(node));
    node.config = *config;
    if (hermes_mkdir_p(config->root) != 0) {
        return HERMES_ERR_IO;
    }
    if (hermes_relay_make_paths(&node) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    if (node.config.log_path[0] == '\0' &&
        hermes_relay_default_log_path(config->root, node.config.log_path, sizeof(node.config.log_path)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    if (hermes_mkdir_p(node.store_dir) != 0 ||
        hermes_mkdir_p(node.import_dir) != 0 ||
        hermes_mkdir_p(node.archive_dir) != 0 ||
        hermes_mkdir_p(node.export_dir) != 0 ||
        hermes_mkdir_p(node.contacts_dir) != 0 ||
        hermes_mkdir_p(node.logs_dir) != 0 ||
        hermes_mkdir_p(node.run_dir) != 0) {
        return HERMES_ERR_IO;
    }
    node.config = *config;
    if (node.config.log_path[0] == '\0') {
        if (hermes_relay_default_log_path(config->root, node.config.log_path, sizeof(node.config.log_path)) != HERMES_OK) {
            return HERMES_ERR_RANGE;
        }
    }
    if (!hermes_path_exists(node.peers_path) &&
        hermes_write_file_atomic(node.peers_path, &empty_file, 0u) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    return hermes_relay_save_config(&node.config);
}

hermes_status hermes_relay_load_config(const char *root, hermes_relay_config *config) {
    FILE *fp;
    char path[1024];
    char line[256];
    if (!root || !config) {
        return HERMES_ERR_ARGUMENT;
    }
    hermes_relay_default_config(config);
    if (strlen(root) >= sizeof(config->root)) {
        return HERMES_ERR_RANGE;
    }
    memcpy(config->root, root, strlen(root) + 1u);
    if (hermes_join_path(path, sizeof(path), root, HERMES_RELAY_CONFIG_NAME) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return HERMES_ERR_IO;
    }
    while (fgets(line, sizeof(line), fp)) {
        char *eq;
        if (!strchr(line, '\n') && !feof(fp)) {
            fclose(fp);
            return HERMES_ERR_RANGE;
        }
        hermes_trim(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            fclose(fp);
            return HERMES_ERR_FORMAT;
        }
        *eq++ = '\0';
        hermes_trim(line);
        hermes_trim(eq);
        if (strcmp(line, "listen_addr") == 0) {
            if (strlen(eq) >= sizeof(config->listen_addr)) {
                fclose(fp);
                return HERMES_ERR_RANGE;
            }
            memcpy(config->listen_addr, eq, strlen(eq) + 1u);
        } else if (strcmp(line, "log_path") == 0) {
            if (strlen(eq) >= sizeof(config->log_path)) {
                fclose(fp);
                return HERMES_ERR_RANGE;
            }
            memcpy(config->log_path, eq, strlen(eq) + 1u);
        } else if (strcmp(line, "sync_interval_seconds") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->sync_interval_seconds)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "import_interval_seconds") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->import_interval_seconds)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "export_interval_seconds") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->export_interval_seconds)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "cleanup_interval_seconds") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->cleanup_interval_seconds)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "heartbeat_interval_seconds") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->heartbeat_interval_seconds)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "log_rotate_bytes") == 0) {
            if (!hermes_parse_u64_strict(eq, &config->log_rotate_bytes)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "log_rotate_keep") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->log_rotate_keep)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "log_mirror_stderr") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->log_mirror_stderr)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "auto_learn_peers") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->auto_learn_peers)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "sync_on_change") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->sync_on_change)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "max_peers") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->max_peers)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "peer_failure_threshold") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->peer_failure_threshold)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else if (strcmp(line, "peer_prune_stale_seconds") == 0) {
            if (!hermes_parse_u32_strict(eq, &config->peer_prune_stale_seconds)) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
        } else {
            fclose(fp);
            return HERMES_ERR_FORMAT;
        }
    }
    fclose(fp);
    if (config->log_path[0] == '\0') {
        if (hermes_relay_default_log_path(root, config->log_path, sizeof(config->log_path)) != HERMES_OK) {
            return HERMES_ERR_RANGE;
        }
    }
    return HERMES_OK;
}

hermes_status hermes_relay_open(hermes_relay_node *node, const hermes_relay_config *config) {
    hermes_store_policy policy;
    if (!node || !config) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(node, 0, sizeof(*node));
    node->config = *config;
    if (node->config.log_path[0] == '\0' &&
        hermes_relay_default_log_path(node->config.root, node->config.log_path, sizeof(node->config.log_path)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    if (hermes_relay_make_paths(node) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    hermes_store_default_policy(&policy);
    return hermes_store_open(&node->store, node->store_dir, &policy);
}

hermes_status hermes_relay_add_peer(const char *root, const char *peer_addr, const char *alias) {
    char path[1024];
    hermes_relay_config config;
    hermes_relay_peer *peers = NULL;
    hermes_relay_peer peer;
    size_t count = 0;
    int existing = -1;
    hermes_status status;
    if (!root || !peer_addr) {
        return HERMES_ERR_ARGUMENT;
    }
    if (!hermes_relay_address_allowed(peer_addr, sizeof(peer.address)) ||
        !hermes_relay_alias_allowed(alias ? alias : "", sizeof(peer.alias))) {
        return HERMES_ERR_RANGE;
    }
    if (hermes_join_path(path, sizeof(path), root, HERMES_RELAY_PEERS_NAME) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    status = hermes_relay_load_config(root, &config);
    if (status != HERMES_OK) {
        hermes_relay_default_config(&config);
        if (strlen(root) >= sizeof(config.root)) {
            return HERMES_ERR_RANGE;
        }
        memcpy(config.root, root, strlen(root) + 1u);
    }
    status = hermes_relay_load_peer_file(path, &peers, &count);
    if (status != HERMES_OK) {
        return status;
    }
    memset(&peer, 0, sizeof(peer));
    existing = hermes_relay_find_peer_index(peers, count, peer_addr);
    if (existing >= 0) {
        peer = peers[existing];
    }
    memcpy(peer.address, peer_addr, strlen(peer_addr) + 1u);
    if (alias && alias[0] != '\0') {
        memcpy(peer.alias, alias, strlen(alias) + 1u);
    }
    peer.learned_automatically = 0u;
    peer.inactive = 0u;
    status = hermes_relay_upsert_peer_file(path, &peer, config.max_peers);
    free(peers);
    return status;
}

hermes_status hermes_relay_list_peers(const char *root,
                                      hermes_relay_peer *peers,
                                      size_t max_peers,
                                      size_t *out_count) {
    char path[1024];
    hermes_relay_peer *items = NULL;
    size_t count = 0;
    size_t i;
    hermes_status status;
    if (!root || !peers || !out_count) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_join_path(path, sizeof(path), root, HERMES_RELAY_PEERS_NAME) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    status = hermes_relay_load_peer_file(path, &items, &count);
    if (status != HERMES_OK) {
        return status;
    }
    if (count > max_peers) {
        free(items);
        return HERMES_ERR_RANGE;
    }
    for (i = 0; i < count; ++i) {
        peers[i] = items[i];
    }
    free(items);
    *out_count = count;
    return HERMES_OK;
}

static hermes_status hermes_relay_process_imports_internal(hermes_relay_node *node,
                                                           size_t *processed,
                                                           hermes_relay_runtime *runtime) {
    hermes_dir_iter dir;
    hermes_dir_entry entry;
    size_t total = 0;
    if (!node || !processed) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_dir_open(node->import_dir, &dir) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    while (hermes_dir_next(&dir, &entry) > 0) {
        char src[1024];
        char dst[1024];
        size_t accepted = 0;
        hermes_status status;
        if (!entry.is_regular) {
            continue;
        }
        if (hermes_join_path(src, sizeof(src), node->import_dir, entry.name) != HERMES_OK) {
            continue;
        }
        if (hermes_relay_check_regular_file_limit(src, HERMES_RELAY_MAX_IMPORT_FILE_SIZE) != HERMES_OK) {
            continue;
        }
        status = hermes_bundle_import_store(&node->store, src, (uint64_t) hermes_now_utc(), &accepted);
        if (status != HERMES_OK) {
            status = hermes_relay_import_single_envelope(node, src, (uint64_t) hermes_now_utc(), &accepted);
        }
        if (status == HERMES_OK) {
            if (hermes_relay_archive_path(node, entry.name, dst, sizeof(dst)) == HERMES_OK) {
                (void) hermes_platform_rename(src, dst);
            }
            ++total;
        } else if (runtime) {
            hermes_log_field fields[] = {
                {"path", HERMES_LOG_FIELD_STRING, entry.name, 0u, 0},
                {"status_code", HERMES_LOG_FIELD_U64, NULL, (uint64_t) (uint32_t) (-status), 0}
            };
            ++runtime->import_failures;
            (void) hermes_relay_log(runtime, "warn", "import_rejected", fields, 2u);
        }
    }
    hermes_dir_close(&dir);
    if (runtime) {
        runtime->last_import_at_unix = (uint64_t) hermes_now_utc();
        if (total > 0u) {
            hermes_log_field fields[] = {
                {"processed", HERMES_LOG_FIELD_U64, NULL, total, 0}
            };
            (void) hermes_relay_log(runtime, "info", "imports_processed", fields, 1u);
        }
    }
    *processed = total;
    return HERMES_OK;
}

hermes_status hermes_relay_process_imports(hermes_relay_node *node, size_t *processed) {
    return hermes_relay_process_imports_internal(node, processed, NULL);
}

static hermes_status hermes_relay_export_latest_internal(hermes_relay_node *node,
                                                         hermes_relay_runtime *runtime) {
    char path[1024];
    hermes_status status;
    if (!node) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_join_path(path, sizeof(path), node->export_dir, "latest.bundle") != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    status = hermes_bundle_export_store(&node->store, path, (uint64_t) hermes_now_utc());
    if (status == HERMES_OK && runtime) {
        runtime->last_export_at_unix = (uint64_t) hermes_now_utc();
    }
    return status;
}

hermes_status hermes_relay_export_latest(hermes_relay_node *node) {
    return hermes_relay_export_latest_internal(node, NULL);
}

static hermes_status hermes_relay_sync_once_internal(hermes_relay_node *node,
                                                     size_t *synced_peers,
                                                     hermes_relay_runtime *runtime) {
    hermes_relay_peer *peers = NULL;
    size_t count = 0;
    size_t i;
    size_t success = 0;
    size_t failures = 0;
    hermes_status status;
    if (!node || !synced_peers) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_relay_load_peer_file(node->peers_path, &peers, &count);
    if (status != HERMES_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        hermes_status sync_status;
        if (peers[i].address[0] == '\0') {
            continue;
        }
        peers[i].last_attempt_unix = (uint64_t) hermes_now_utc();
        sync_status = hermes_transport_tcp_sync_peer_claim(&node->store,
                                                           peers[i].address,
                                                           peers[i].last_attempt_unix,
                                                           node->config.listen_addr,
                                                           NULL,
                                                           0u);
        if (sync_status == HERMES_OK) {
            peers[i].last_success_unix = peers[i].last_attempt_unix;
            peers[i].consecutive_failures = 0u;
            peers[i].inactive = 0u;
            ++success;
        } else {
            ++failures;
            if (peers[i].consecutive_failures < UINT32_MAX) {
                ++peers[i].consecutive_failures;
            }
            if (node->config.peer_failure_threshold > 0u &&
                peers[i].consecutive_failures >= node->config.peer_failure_threshold) {
                peers[i].inactive = 1u;
            }
        }
    }
    hermes_relay_prune_auto_peers(peers, &count, &node->config, (uint64_t) hermes_now_utc());
    status = hermes_relay_save_peer_file(node->peers_path, peers, count);
    free(peers);
    if (status != HERMES_OK) {
        return status;
    }
    if (runtime) {
        runtime->last_sync_at_unix = (uint64_t) hermes_now_utc();
        runtime->sync_failures += failures;
        {
            hermes_log_field fields[] = {
                {"peers_total", HERMES_LOG_FIELD_U64, NULL, count, 0},
                {"peers_synced", HERMES_LOG_FIELD_U64, NULL, success, 0},
                {"peers_failed", HERMES_LOG_FIELD_U64, NULL, failures, 0}
            };
            (void) hermes_relay_log(runtime, failures > 0u ? "warn" : "info", "sync_pass", fields, 3u);
        }
    }
    *synced_peers = success;
    return HERMES_OK;
}

hermes_status hermes_relay_sync_once(hermes_relay_node *node, size_t *synced_peers) {
    return hermes_relay_sync_once_internal(node, synced_peers, NULL);
}

hermes_status hermes_relay_run(hermes_relay_node *node) {
    hermes_socket_handle listen_fd = (hermes_socket_handle) -1;
    uint64_t next_sync_at;
    uint64_t next_import_at;
    uint64_t next_export_at;
    uint64_t next_cleanup_at;
    uint64_t next_heartbeat_at;
    hermes_relay_runtime runtime;
    hermes_status status;
    if (!node) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_transport_tcp_open_listener(node->config.listen_addr, &listen_fd);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_relay_runtime_open(node, &runtime);
    if (status != HERMES_OK) {
        hermes_transport_tcp_close_listener(listen_fd);
        return status;
    }
    (void) hermes_relay_write_status(node, &runtime, "starting", runtime.started_at_unix);
    {
        hermes_log_field fields[] = {
            {"listen_addr", HERMES_LOG_FIELD_STRING, node->config.listen_addr, 0u, 0},
            {"log_path", HERMES_LOG_FIELD_STRING, node->config.log_path, 0u, 0},
            {"pid", HERMES_LOG_FIELD_U64, NULL, (uint64_t) hermes_relay_pid(), 0}
        };
        (void) hermes_relay_log(&runtime, "info", "service_started", fields, 3u);
    }
    next_sync_at = (uint64_t) hermes_now_utc();
    next_import_at = next_sync_at;
    next_export_at = next_sync_at;
    next_cleanup_at = next_sync_at;
    next_heartbeat_at = next_sync_at;
    while (runtime.signals.stop_requested == 0) {
        uint64_t now_unix = (uint64_t) hermes_now_utc();
        if (hermes_relay_consume_log_reopen(&runtime)) {
            if (hermes_logger_reopen(&runtime.logger) == HERMES_OK) {
                (void) hermes_relay_log(&runtime, "info", "log_reopened", NULL, 0u);
            }
        }
        if (hermes_transport_tcp_wait_listener(listen_fd, 1000u)) {
            char learned_peer[128];
            hermes_status accept_status;
            learned_peer[0] = '\0';
            accept_status = hermes_transport_tcp_accept_once_peer(listen_fd,
                                                                  &node->store,
                                                                  node->config.listen_addr,
                                                                  learned_peer,
                                                                  sizeof(learned_peer));
            if (accept_status == HERMES_OK) {
                ++runtime.inbound_sessions;
                if (node->config.auto_learn_peers != 0u &&
                    learned_peer[0] != '\0' &&
                    strcmp(learned_peer, node->config.listen_addr) != 0) {
                    hermes_relay_peer peer;
                    memset(&peer, 0, sizeof(peer));
                    memcpy(peer.address, learned_peer, strlen(learned_peer) + 1u);
                    peer.learned_automatically = 1u;
                    peer.inactive = 0u;
                    peer.last_success_unix = (uint64_t) hermes_now_utc();
                    peer.last_attempt_unix = peer.last_success_unix;
                    if (hermes_relay_upsert_peer_file(node->peers_path, &peer, node->config.max_peers) == HERMES_OK) {
                        ++runtime.auto_learn_events;
                        {
                            hermes_log_field fields[] = {
                                {"peer", HERMES_LOG_FIELD_STRING, learned_peer, 0u, 0}
                            };
                            (void) hermes_relay_log(&runtime, "info", "peer_auto_learned", fields, 1u);
                        }
                    }
                }
                if (node->config.sync_on_change != 0u) {
                    size_t synced = 0;
                    (void) hermes_relay_sync_once_internal(node, &synced, &runtime);
                }
            } else {
                hermes_log_field fields[] = {
                    {"status_code", HERMES_LOG_FIELD_U64, NULL, (uint64_t) (uint32_t) (-accept_status), 0}
                };
                (void) hermes_relay_log(&runtime, "warn", "inbound_session_failed", fields, 1u);
            }
        }
        now_unix = (uint64_t) hermes_now_utc();
        if (now_unix >= next_import_at) {
            size_t processed = 0;
            (void) hermes_relay_process_imports_internal(node, &processed, &runtime);
            if (processed > 0u && node->config.sync_on_change != 0u) {
                size_t synced = 0;
                (void) hermes_relay_sync_once_internal(node, &synced, &runtime);
            }
            next_import_at = now_unix + (node->config.import_interval_seconds ? node->config.import_interval_seconds : 15u);
        }
        if (now_unix >= next_sync_at) {
            size_t synced = 0;
            (void) hermes_relay_sync_once_internal(node, &synced, &runtime);
            next_sync_at = now_unix + (node->config.sync_interval_seconds ? node->config.sync_interval_seconds : 300u);
        }
        if (now_unix >= next_export_at) {
            (void) hermes_relay_export_latest_internal(node, &runtime);
            next_export_at = now_unix + (node->config.export_interval_seconds ? node->config.export_interval_seconds : 300u);
        }
        if (now_unix >= next_cleanup_at) {
            (void) hermes_store_cleanup(&node->store, now_unix);
            runtime.last_cleanup_at_unix = now_unix;
            next_cleanup_at = now_unix + (node->config.cleanup_interval_seconds ? node->config.cleanup_interval_seconds : 300u);
        }
        if (now_unix >= next_heartbeat_at) {
            (void) hermes_relay_write_status(node, &runtime, "running", now_unix);
            next_heartbeat_at = now_unix +
                                (node->config.heartbeat_interval_seconds ? node->config.heartbeat_interval_seconds :
                                                                           HERMES_RELAY_DEFAULT_HEARTBEAT_INTERVAL);
        }
    }
    (void) hermes_relay_write_status(node, &runtime, "stopped", (uint64_t) hermes_now_utc());
    (void) hermes_relay_log(&runtime, "info", "service_stopped", NULL, 0u);
    hermes_relay_runtime_close(node, &runtime);
    hermes_transport_tcp_close_listener(listen_fd);
    return HERMES_OK;
}

hermes_status hermes_relay_read_status(const char *root, uint8_t **data, size_t *len) {
    char run_dir[1024];
    char status_path[1024];
    hermes_status status;
    if (!root || !data || !len) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_join_path(run_dir, sizeof(run_dir), root, "run");
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_join_path(status_path, sizeof(status_path), run_dir, "status.json");
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_read_file(status_path, data, len);
}
