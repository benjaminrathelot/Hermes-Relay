#ifndef HERMES_STORE_H
#define HERMES_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"
#include "types.h"

typedef struct hermes_store {
    char root[512];
    hermes_store_policy policy;
} hermes_store;

void hermes_store_default_policy(hermes_store_policy *policy);
hermes_status hermes_store_open(hermes_store *store, const char *root, const hermes_store_policy *policy);

hermes_status hermes_store_add_envelope(hermes_store *store, const hermes_envelope *env, uint64_t now_unix);
hermes_status hermes_store_get_envelope(hermes_store *store,
                                        const uint8_t envelope_id[HERMES_ID_LEN],
                                        hermes_envelope *env);
hermes_status hermes_store_has_seen_id(hermes_store *store, const uint8_t envelope_id[HERMES_ID_LEN]);
hermes_status hermes_store_has_seen_hash(hermes_store *store, const uint8_t hash[HERMES_HASH_LEN]);
hermes_status hermes_store_mark_seen_id(hermes_store *store,
                                        const uint8_t envelope_id[HERMES_ID_LEN],
                                        uint64_t seen_at_unix);
hermes_status hermes_store_mark_seen_hash(hermes_store *store,
                                          const uint8_t hash[HERMES_HASH_LEN],
                                          uint64_t seen_at_unix);
hermes_status hermes_store_first_seen(hermes_store *store,
                                      const uint8_t envelope_id[HERMES_ID_LEN],
                                      uint64_t *first_seen_at_unix);
int hermes_store_can_forward(hermes_store *store, const hermes_envelope *env, uint64_t now_unix);
hermes_status hermes_store_list_inventory(hermes_store *store,
                                          uint8_t *ids,
                                          size_t max_ids,
                                          size_t *out_ids);
hermes_status hermes_store_get_stats(hermes_store *store, hermes_store_stats *stats, uint64_t now_unix);
hermes_status hermes_store_cleanup(hermes_store *store, uint64_t now_unix);
hermes_status hermes_store_enforce_quotas(hermes_store *store, uint64_t now_unix);

#endif
