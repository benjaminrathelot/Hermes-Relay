#ifndef HERMES_TYPES_H
#define HERMES_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "version.h"

typedef struct hermes_public_identity {
    uint8_t sig_public[HERMES_SIG_PUBLIC_LEN];
    uint8_t box_public[HERMES_BOX_PUBLIC_LEN];
} hermes_public_identity;

typedef struct hermes_identity {
    hermes_public_identity public_identity;
    uint8_t sig_private[HERMES_SIG_PRIVATE_LEN];
    uint8_t box_private[HERMES_BOX_PRIVATE_LEN];
    char alias[64];
} hermes_identity;

typedef struct hermes_contact {
    hermes_public_identity public_identity;
    uint8_t recipient_hint[HERMES_RECIPIENT_HINT_LEN];
    uint8_t fingerprint[HERMES_HASH_LEN];
    char alias[64];
} hermes_contact;

typedef struct hermes_envelope {
    uint8_t protocol_version;
    uint8_t crypto_suite_id;
    uint8_t pow_algorithm;
    uint8_t reserved_header;
    uint16_t flags;
    uint16_t reserved_len;
    uint8_t envelope_id[HERMES_ID_LEN];
    uint64_t created_at_unix;
    uint64_t expires_at_unix;
    hermes_public_identity sender_public_identity;
    uint8_t recipient_hint[HERMES_RECIPIENT_HINT_LEN];
    uint16_t payload_size;
    uint8_t encrypted_payload[HERMES_MAX_ENCRYPTED_PAYLOAD];
    uint8_t signature[HERMES_SIGNATURE_LEN];
    uint32_t pow_difficulty;
    uint64_t pow_nonce;
    uint8_t reserved[HERMES_MAX_RESERVED];
    size_t encoded_size;
} hermes_envelope;

typedef struct hermes_store_policy {
    uint64_t max_store_bytes;
    uint32_t max_envelopes;
    uint32_t max_envelope_size;
    uint32_t max_ttl_seconds;
    uint32_t min_pow_difficulty;
    uint32_t max_future_skew_seconds;
    uint32_t max_messages_per_sender_per_day;
    uint32_t max_messages_per_recipient_per_day;
    uint32_t dedup_retention_seconds;
} hermes_store_policy;

typedef struct hermes_store_stats {
    uint64_t envelope_count;
    uint64_t total_bytes;
    uint64_t expired_count;
    uint64_t weakest_pow;
    uint64_t strongest_pow;
} hermes_store_stats;

#endif
