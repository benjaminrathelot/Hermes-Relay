#ifndef HERMES_STATUS_H
#define HERMES_STATUS_H

typedef enum hermes_status {
    HERMES_OK = 0,
    HERMES_ERR_ARGUMENT = -1,
    HERMES_ERR_RANGE = -2,
    HERMES_ERR_FORMAT = -3,
    HERMES_ERR_CRYPTO = -4,
    HERMES_ERR_IO = -5,
    HERMES_ERR_NOT_FOUND = -6,
    HERMES_ERR_DUPLICATE = -7,
    HERMES_ERR_QUOTA = -8,
    HERMES_ERR_EXPIRED = -9,
    HERMES_ERR_VERIFY = -10,
    HERMES_ERR_PROTOCOL = -11,
    HERMES_ERR_MEMORY = -12
} hermes_status;

const char *hermes_status_string(hermes_status status);

#endif

