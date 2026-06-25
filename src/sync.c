#include "hermes/sync.h"

#include <string.h>

#include "hermes/version.h"

static int hermes_id_present(const uint8_t *ids, size_t count, const uint8_t id[HERMES_ID_LEN]) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (memcmp(ids + (i * HERMES_ID_LEN), id, HERMES_ID_LEN) == 0) {
            return 1;
        }
    }
    return 0;
}

hermes_status hermes_inventory_diff(const uint8_t *local_ids,
                                    size_t local_count,
                                    const uint8_t *remote_ids,
                                    size_t remote_count,
                                    uint8_t *missing_ids,
                                    size_t max_missing_ids,
                                    size_t *missing_count) {
    size_t i;
    size_t count = 0;
    if ((!local_ids && local_count > 0) || (!remote_ids && remote_count > 0) || !missing_ids || !missing_count) {
        return HERMES_ERR_ARGUMENT;
    }
    for (i = 0; i < remote_count; ++i) {
        const uint8_t *id = remote_ids + (i * HERMES_ID_LEN);
        if (!hermes_id_present(local_ids, local_count, id)) {
            if (count >= max_missing_ids) {
                return HERMES_ERR_RANGE;
            }
            memcpy(missing_ids + (count * HERMES_ID_LEN), id, HERMES_ID_LEN);
            ++count;
        }
    }
    *missing_count = count;
    return HERMES_OK;
}
