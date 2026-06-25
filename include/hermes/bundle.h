#ifndef HERMES_BUNDLE_H
#define HERMES_BUNDLE_H

#include <stddef.h>

#include "status.h"
#include "store.h"

hermes_status hermes_bundle_export_store(hermes_store *store, const char *path, uint64_t now_unix);
hermes_status hermes_bundle_import_store(hermes_store *store, const char *path, uint64_t now_unix, size_t *imported);
hermes_status hermes_bundle_encode_envelopes(const hermes_envelope *envs,
                                             size_t env_count,
                                             uint8_t **bundle_bytes,
                                             size_t *bundle_len);
hermes_status hermes_bundle_decode_envelopes(const uint8_t *bundle_bytes,
                                             size_t bundle_len,
                                             hermes_envelope **envs,
                                             size_t *env_count);

#endif

