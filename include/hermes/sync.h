#ifndef HERMES_SYNC_H
#define HERMES_SYNC_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

hermes_status hermes_inventory_diff(const uint8_t *local_ids,
                                    size_t local_count,
                                    const uint8_t *remote_ids,
                                    size_t remote_count,
                                    uint8_t *missing_ids,
                                    size_t max_missing_ids,
                                    size_t *missing_count);

#endif

