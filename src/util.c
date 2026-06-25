#include "hermes/util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "hermes/platform.h"

uint16_t hermes_read_be16(const uint8_t *src) {
    return (uint16_t) (((uint16_t) src[0] << 8) | (uint16_t) src[1]);
}

uint32_t hermes_read_be32(const uint8_t *src) {
    return ((uint32_t) src[0] << 24) |
           ((uint32_t) src[1] << 16) |
           ((uint32_t) src[2] << 8) |
           (uint32_t) src[3];
}

uint64_t hermes_read_be64(const uint8_t *src) {
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; ++i) {
        value = (value << 8) | src[i];
    }
    return value;
}

void hermes_write_be16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t) (value >> 8);
    dst[1] = (uint8_t) value;
}

void hermes_write_be32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t) (value >> 24);
    dst[1] = (uint8_t) (value >> 16);
    dst[2] = (uint8_t) (value >> 8);
    dst[3] = (uint8_t) value;
}

void hermes_write_be64(uint8_t *dst, uint64_t value) {
    size_t i;
    for (i = 0; i < 8; ++i) {
        dst[7 - i] = (uint8_t) (value & 0xffu);
        value >>= 8;
    }
}

void hermes_secure_bzero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *) ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

int hermes_ct_equal(const uint8_t *lhs, const uint8_t *rhs, size_t len) {
    size_t i;
    uint8_t diff = 0;
    for (i = 0; i < len; ++i) {
        diff |= (uint8_t) (lhs[i] ^ rhs[i]);
    }
    return diff == 0;
}

static int hermes_hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

hermes_status hermes_hex_encode(const uint8_t *src, size_t src_len, char *dst, size_t dst_len) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    if (!src || !dst) {
        return HERMES_ERR_ARGUMENT;
    }
    if (dst_len < (src_len * 2u) + 1u) {
        return HERMES_ERR_RANGE;
    }
    for (i = 0; i < src_len; ++i) {
        dst[i * 2u] = hex[(src[i] >> 4) & 0x0f];
        dst[(i * 2u) + 1u] = hex[src[i] & 0x0f];
    }
    dst[src_len * 2u] = '\0';
    return HERMES_OK;
}

hermes_status hermes_hex_decode(const char *src, uint8_t *dst, size_t dst_len) {
    size_t i;
    size_t src_len;
    if (!src || !dst) {
        return HERMES_ERR_ARGUMENT;
    }
    src_len = strlen(src);
    if (src_len != dst_len * 2u) {
        return HERMES_ERR_FORMAT;
    }
    for (i = 0; i < dst_len; ++i) {
        int hi = hermes_hex_nibble(src[i * 2u]);
        int lo = hermes_hex_nibble(src[(i * 2u) + 1u]);
        if (hi < 0 || lo < 0) {
            return HERMES_ERR_FORMAT;
        }
        dst[i] = (uint8_t) ((hi << 4) | lo);
    }
    return HERMES_OK;
}

time_t hermes_now_utc(void) {
    return time(NULL);
}

int hermes_path_exists(const char *path) {
    return hermes_platform_path_exists(path);
}

int hermes_is_regular_file(const char *path) {
    return hermes_platform_is_regular_file(path);
}

int hermes_mkdir_p(const char *path) {
    char buffer[1024];
    size_t i;
    size_t len;
    if (!path) {
        return -1;
    }
    len = strlen(path);
    if (len == 0 || len >= sizeof(buffer)) {
        return -1;
    }
    memcpy(buffer, path, len + 1u);
    for (i = 1; i < len; ++i) {
        if (buffer[i] == '/') {
            buffer[i] = '\0';
            if (buffer[0] != '\0' && hermes_platform_mkdir(buffer) != 0 && errno != EEXIST) {
                return -1;
            }
            buffer[i] = '/';
        }
    }
    if (hermes_platform_mkdir(buffer) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

hermes_status hermes_read_file(const char *path, uint8_t **data, size_t *len) {
    FILE *fp;
    long raw_len;
    uint8_t *buffer;
    size_t read_len;

    if (!path || !data || !len) {
        return HERMES_ERR_ARGUMENT;
    }
    *data = NULL;
    *len = 0;

    fp = fopen(path, "rb");
    if (!fp) {
        return HERMES_ERR_IO;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return HERMES_ERR_IO;
    }
    raw_len = ftell(fp);
    if (raw_len < 0) {
        fclose(fp);
        return HERMES_ERR_IO;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return HERMES_ERR_IO;
    }
    buffer = (uint8_t *) malloc((size_t) raw_len);
    if (raw_len > 0 && !buffer) {
        fclose(fp);
        return HERMES_ERR_MEMORY;
    }
    read_len = raw_len == 0 ? 0u : fread(buffer, 1u, (size_t) raw_len, fp);
    fclose(fp);
    if (read_len != (size_t) raw_len) {
        free(buffer);
        return HERMES_ERR_IO;
    }
    *data = buffer;
    *len = read_len;
    return HERMES_OK;
}

hermes_status hermes_write_file_atomic(const char *path, const uint8_t *data, size_t len) {
    char tmp_path[1024];
    FILE *fp;
    const char *slash;
    size_t parent_len;

    if (!path || (!data && len > 0)) {
        return HERMES_ERR_ARGUMENT;
    }

    slash = strrchr(path, '/');
    if (slash) {
        char parent[1024];
        parent_len = (size_t) (slash - path);
        if (parent_len >= sizeof(parent)) {
            return HERMES_ERR_RANGE;
        }
        memcpy(parent, path, parent_len);
        parent[parent_len] = '\0';
        if (hermes_mkdir_p(parent) != 0) {
            return HERMES_ERR_IO;
        }
    }

    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%lu", path, hermes_platform_process_id()) >= (int) sizeof(tmp_path)) {
        return HERMES_ERR_RANGE;
    }
    fp = fopen(tmp_path, "wb");
    if (!fp) {
        return HERMES_ERR_IO;
    }
    if (len > 0 && fwrite(data, 1u, len, fp) != len) {
        fclose(fp);
        hermes_platform_remove_file(tmp_path);
        return HERMES_ERR_IO;
    }
    if (fclose(fp) != 0) {
        hermes_platform_remove_file(tmp_path);
        return HERMES_ERR_IO;
    }
    if (hermes_platform_rename(tmp_path, path) != 0) {
        hermes_platform_remove_file(tmp_path);
        return HERMES_ERR_IO;
    }
    return HERMES_OK;
}

hermes_status hermes_append_text_line(const char *path, const char *line) {
    FILE *fp;
    if (!path || !line) {
        return HERMES_ERR_ARGUMENT;
    }
    fp = fopen(path, "ab");
    if (!fp) {
        return HERMES_ERR_IO;
    }
    if (fputs(line, fp) == EOF || fputc('\n', fp) == EOF) {
        fclose(fp);
        return HERMES_ERR_IO;
    }
    if (fclose(fp) != 0) {
        return HERMES_ERR_IO;
    }
    return HERMES_OK;
}

hermes_status hermes_join_path(char *dst, size_t dst_len, const char *lhs, const char *rhs) {
    int written;
    if (!dst || !lhs || !rhs) {
        return HERMES_ERR_ARGUMENT;
    }
    written = snprintf(dst, dst_len, "%s/%s", lhs, rhs);
    if (written < 0 || (size_t) written >= dst_len) {
        return HERMES_ERR_RANGE;
    }
    return HERMES_OK;
}

hermes_status hermes_basename_noext(const char *path, char *dst, size_t dst_len) {
    const char *base;
    const char *dot;
    size_t len;
    if (!path || !dst || dst_len == 0) {
        return HERMES_ERR_ARGUMENT;
    }
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    len = dot ? (size_t) (dot - base) : strlen(base);
    if (len + 1u > dst_len) {
        return HERMES_ERR_RANGE;
    }
    memcpy(dst, base, len);
    dst[len] = '\0';
    return HERMES_OK;
}

const char *hermes_status_string(hermes_status status) {
    switch (status) {
        case HERMES_OK: return "ok";
        case HERMES_ERR_ARGUMENT: return "invalid argument";
        case HERMES_ERR_RANGE: return "range error";
        case HERMES_ERR_FORMAT: return "format error";
        case HERMES_ERR_CRYPTO: return "crypto error";
        case HERMES_ERR_IO: return "I/O error";
        case HERMES_ERR_NOT_FOUND: return "not found";
        case HERMES_ERR_DUPLICATE: return "duplicate";
        case HERMES_ERR_QUOTA: return "quota rejected";
        case HERMES_ERR_EXPIRED: return "expired";
        case HERMES_ERR_VERIFY: return "verification failed";
        case HERMES_ERR_PROTOCOL: return "protocol error";
        case HERMES_ERR_MEMORY: return "memory error";
        default: return "unknown error";
    }
}
