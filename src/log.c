#include "hermes/log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hermes/platform.h"
#include "hermes/util.h"

static hermes_status hermes_log_parent_dir(const char *path, char *parent, size_t parent_len) {
    const char *slash;
    size_t dir_len;
    if (!path || !parent || parent_len == 0u) {
        return HERMES_ERR_ARGUMENT;
    }
    slash = strrchr(path, '/');
    if (!slash) {
        if (parent_len < 2u) {
            return HERMES_ERR_RANGE;
        }
        memcpy(parent, ".", 2u);
        return HERMES_OK;
    }
    dir_len = (size_t) (slash - path);
    if (dir_len == 0u) {
        if (parent_len < 2u) {
            return HERMES_ERR_RANGE;
        }
        memcpy(parent, "/", 2u);
        return HERMES_OK;
    }
    if (dir_len + 1u > parent_len) {
        return HERMES_ERR_RANGE;
    }
    memcpy(parent, path, dir_len);
    parent[dir_len] = '\0';
    return HERMES_OK;
}

static hermes_status hermes_log_rotate_chain(const hermes_logger *logger) {
    uint32_t idx;
    if (!logger) {
        return HERMES_ERR_ARGUMENT;
    }
    if (logger->rotate_keep == 0u) {
        if (hermes_platform_remove_file(logger->path) != 0 && errno != ENOENT) {
            return HERMES_ERR_IO;
        }
        return HERMES_OK;
    }
    for (idx = logger->rotate_keep; idx > 0u; --idx) {
        char src[640];
        char dst[640];
        int written_src;
        int written_dst;
        if (idx == logger->rotate_keep) {
            written_dst = snprintf(dst, sizeof(dst), "%s.%u", logger->path, idx);
            if (written_dst < 0 || (size_t) written_dst >= sizeof(dst)) {
                return HERMES_ERR_RANGE;
            }
            if (hermes_platform_remove_file(dst) != 0 && errno != ENOENT) {
                return HERMES_ERR_IO;
            }
        }
        if (idx == 1u) {
            written_src = snprintf(src, sizeof(src), "%s", logger->path);
        } else {
            written_src = snprintf(src, sizeof(src), "%s.%u", logger->path, idx - 1u);
        }
        written_dst = snprintf(dst, sizeof(dst), "%s.%u", logger->path, idx);
        if (written_src < 0 || written_dst < 0 ||
            (size_t) written_src >= sizeof(src) || (size_t) written_dst >= sizeof(dst)) {
            return HERMES_ERR_RANGE;
        }
        if (hermes_platform_rename(src, dst) != 0 && errno != ENOENT) {
            return HERMES_ERR_IO;
        }
    }
    return HERMES_OK;
}

static hermes_status hermes_log_maybe_rotate(hermes_logger *logger, size_t pending_len) {
    struct stat st;
    if (!logger || !logger->fp) {
        return HERMES_ERR_ARGUMENT;
    }
    if (logger->rotate_bytes == 0u) {
        return HERMES_OK;
    }
    if (fflush(logger->fp) != 0) {
        return HERMES_ERR_IO;
    }
    if (stat(logger->path, &st) != 0) {
        return errno == ENOENT ? HERMES_OK : HERMES_ERR_IO;
    }
    if ((uint64_t) st.st_size + pending_len < logger->rotate_bytes) {
        return HERMES_OK;
    }
    fclose(logger->fp);
    logger->fp = NULL;
    if (hermes_log_rotate_chain(logger) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    return hermes_logger_open(logger);
}

static hermes_status hermes_log_json_escape(const char *src, char *dst, size_t dst_len) {
    size_t out = 0;
    size_t i;
    if (!src || !dst || dst_len == 0u) {
        return HERMES_ERR_ARGUMENT;
    }
    for (i = 0; src[i] != '\0'; ++i) {
        const char *replacement = NULL;
        char esc[7];
        size_t rep_len;
        unsigned char ch = (unsigned char) src[i];
        switch (ch) {
            case '\"': replacement = "\\\""; break;
            case '\\': replacement = "\\\\"; break;
            case '\b': replacement = "\\b"; break;
            case '\f': replacement = "\\f"; break;
            case '\n': replacement = "\\n"; break;
            case '\r': replacement = "\\r"; break;
            case '\t': replacement = "\\t"; break;
            default:
                if (ch < 0x20u) {
                    if (snprintf(esc, sizeof(esc), "\\u%04x", ch) >= (int) sizeof(esc)) {
                        return HERMES_ERR_RANGE;
                    }
                    replacement = esc;
                }
                break;
        }
        if (replacement) {
            rep_len = strlen(replacement);
            if (out + rep_len + 1u > dst_len) {
                return HERMES_ERR_RANGE;
            }
            memcpy(dst + out, replacement, rep_len);
            out += rep_len;
        } else {
            if (out + 2u > dst_len) {
                return HERMES_ERR_RANGE;
            }
            dst[out++] = (char) ch;
        }
    }
    dst[out] = '\0';
    return HERMES_OK;
}

void hermes_logger_default(hermes_logger *logger) {
    if (!logger) {
        return;
    }
    memset(logger, 0, sizeof(*logger));
    logger->rotate_bytes = 16u * 1024u * 1024u;
    logger->rotate_keep = 8u;
    logger->mirror_stderr = 0u;
}

hermes_status hermes_logger_open(hermes_logger *logger) {
    char parent[512];
    if (!logger || logger->path[0] == '\0') {
        return HERMES_ERR_ARGUMENT;
    }
    if (logger->fp) {
        fclose(logger->fp);
        logger->fp = NULL;
    }
    if (hermes_log_parent_dir(logger->path, parent, sizeof(parent)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    if (hermes_mkdir_p(parent) != 0) {
        return HERMES_ERR_IO;
    }
    logger->fp = fopen(logger->path, "ab");
    if (!logger->fp) {
        return HERMES_ERR_IO;
    }
    setvbuf(logger->fp, NULL, _IOLBF, 0);
    return HERMES_OK;
}

hermes_status hermes_logger_reopen(hermes_logger *logger) {
    if (!logger) {
        return HERMES_ERR_ARGUMENT;
    }
    return hermes_logger_open(logger);
}

void hermes_logger_close(hermes_logger *logger) {
    if (!logger) {
        return;
    }
    if (logger->fp) {
        fclose(logger->fp);
        logger->fp = NULL;
    }
}

hermes_status hermes_logger_event(hermes_logger *logger,
                                  const char *level,
                                  const char *component,
                                  const char *event,
                                  const hermes_log_field *fields,
                                  size_t field_count) {
    char line[4096];
    char level_escaped[64];
    char component_escaped[64];
    char event_escaped[128];
    size_t used = 0;
    size_t i;
    int written;
    if (!logger || !logger->fp || !level || !component || !event) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_log_json_escape(level, level_escaped, sizeof(level_escaped)) != HERMES_OK ||
        hermes_log_json_escape(component, component_escaped, sizeof(component_escaped)) != HERMES_OK ||
        hermes_log_json_escape(event, event_escaped, sizeof(event_escaped)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    written = snprintf(line,
                       sizeof(line),
                       "{\"ts_unix\":%llu,\"level\":\"%s\",\"component\":\"%s\",\"event\":\"%s\"",
                       (unsigned long long) hermes_now_utc(),
                       level_escaped,
                       component_escaped,
                       event_escaped);
    if (written < 0 || (size_t) written >= sizeof(line)) {
        return HERMES_ERR_RANGE;
    }
    used = (size_t) written;
    for (i = 0; i < field_count; ++i) {
        char key_escaped[64];
        if (!fields[i].key || hermes_log_json_escape(fields[i].key, key_escaped, sizeof(key_escaped)) != HERMES_OK) {
            return HERMES_ERR_RANGE;
        }
        if (fields[i].type == HERMES_LOG_FIELD_STRING) {
            char value_escaped[512];
            if (!fields[i].string_value ||
                hermes_log_json_escape(fields[i].string_value, value_escaped, sizeof(value_escaped)) != HERMES_OK) {
                return HERMES_ERR_RANGE;
            }
            written = snprintf(line + used, sizeof(line) - used, ",\"%s\":\"%s\"", key_escaped, value_escaped);
        } else if (fields[i].type == HERMES_LOG_FIELD_U64) {
            written = snprintf(line + used,
                               sizeof(line) - used,
                               ",\"%s\":%llu",
                               key_escaped,
                               (unsigned long long) fields[i].u64_value);
        } else if (fields[i].type == HERMES_LOG_FIELD_BOOL) {
            written = snprintf(line + used,
                               sizeof(line) - used,
                               ",\"%s\":%s",
                               key_escaped,
                               fields[i].bool_value ? "true" : "false");
        } else {
            return HERMES_ERR_ARGUMENT;
        }
        if (written < 0 || (size_t) written >= sizeof(line) - used) {
            return HERMES_ERR_RANGE;
        }
        used += (size_t) written;
    }
    if (used + 2u > sizeof(line)) {
        return HERMES_ERR_RANGE;
    }
    line[used++] = '}';
    line[used++] = '\n';
    if (hermes_log_maybe_rotate(logger, used) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    if (fwrite(line, 1u, used, logger->fp) != used) {
        return HERMES_ERR_IO;
    }
    if (fflush(logger->fp) != 0) {
        return HERMES_ERR_IO;
    }
    if (logger->mirror_stderr != 0u) {
        (void) fwrite(line, 1u, used, stderr);
        (void) fflush(stderr);
    }
    return HERMES_OK;
}
