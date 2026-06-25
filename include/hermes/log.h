#ifndef HERMES_LOG_H
#define HERMES_LOG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "status.h"

typedef enum hermes_log_field_type {
    HERMES_LOG_FIELD_STRING = 1,
    HERMES_LOG_FIELD_U64 = 2,
    HERMES_LOG_FIELD_BOOL = 3
} hermes_log_field_type;

typedef struct hermes_log_field {
    const char *key;
    hermes_log_field_type type;
    const char *string_value;
    uint64_t u64_value;
    int bool_value;
} hermes_log_field;

typedef struct hermes_logger {
    char path[512];
    uint64_t rotate_bytes;
    uint32_t rotate_keep;
    uint32_t mirror_stderr;
    FILE *fp;
} hermes_logger;

void hermes_logger_default(hermes_logger *logger);
hermes_status hermes_logger_open(hermes_logger *logger);
hermes_status hermes_logger_reopen(hermes_logger *logger);
void hermes_logger_close(hermes_logger *logger);
hermes_status hermes_logger_event(hermes_logger *logger,
                                  const char *level,
                                  const char *component,
                                  const char *event,
                                  const hermes_log_field *fields,
                                  size_t field_count);

#endif
