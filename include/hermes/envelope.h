#ifndef HERMES_ENVELOPE_H
#define HERMES_ENVELOPE_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"
#include "types.h"

void hermes_envelope_init(hermes_envelope *env);

hermes_status hermes_envelope_create(hermes_envelope *env,
                                     const hermes_identity *sender,
                                     const hermes_contact *recipient,
                                     const uint8_t *plaintext,
                                     size_t plaintext_len,
                                     uint64_t created_at_unix,
                                     uint64_t ttl_seconds,
                                     uint32_t flags,
                                     uint32_t pow_difficulty);

hermes_status hermes_envelope_encode(const hermes_envelope *env, uint8_t *out, size_t *out_len);
hermes_status hermes_envelope_decode(const uint8_t *data, size_t data_len, hermes_envelope *env);

hermes_status hermes_envelope_compute_id(hermes_envelope *env);
hermes_status hermes_envelope_sign(hermes_envelope *env, const hermes_identity *sender);
hermes_status hermes_envelope_verify(const hermes_envelope *env, uint64_t now_unix);

hermes_status hermes_envelope_hash_wire(const hermes_envelope *env, uint8_t out[HERMES_HASH_LEN]);
int hermes_envelope_is_expired(const hermes_envelope *env, uint64_t now_unix);

#endif

