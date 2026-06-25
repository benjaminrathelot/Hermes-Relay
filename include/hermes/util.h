#ifndef HERMES_UTIL_H
#define HERMES_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "status.h"

uint16_t hermes_read_be16(const uint8_t *src);
uint32_t hermes_read_be32(const uint8_t *src);
uint64_t hermes_read_be64(const uint8_t *src);
void hermes_write_be16(uint8_t *dst, uint16_t value);
void hermes_write_be32(uint8_t *dst, uint32_t value);
void hermes_write_be64(uint8_t *dst, uint64_t value);

void hermes_secure_bzero(void *ptr, size_t len);
int hermes_ct_equal(const uint8_t *lhs, const uint8_t *rhs, size_t len);

hermes_status hermes_hex_encode(const uint8_t *src, size_t src_len, char *dst, size_t dst_len);
hermes_status hermes_hex_decode(const char *src, uint8_t *dst, size_t dst_len);

time_t hermes_now_utc(void);
int hermes_path_exists(const char *path);
int hermes_is_regular_file(const char *path);
int hermes_mkdir_p(const char *path);
hermes_status hermes_read_file(const char *path, uint8_t **data, size_t *len);
hermes_status hermes_write_file_atomic(const char *path, const uint8_t *data, size_t len);
hermes_status hermes_append_text_line(const char *path, const char *line);
hermes_status hermes_join_path(char *dst, size_t dst_len, const char *lhs, const char *rhs);
hermes_status hermes_basename_noext(const char *path, char *dst, size_t dst_len);

#endif

