#include "hermes/envelope.h"

#include <stdlib.h>
#include <string.h>

#include "hermes/crypto.h"
#include "hermes/util.h"

static size_t hermes_envelope_min_size(void) {
    return 4u + 1u + 1u + 1u + 1u + 2u + 2u + HERMES_ID_LEN + 8u + 8u +
           HERMES_PUBLIC_ID_LEN + HERMES_RECIPIENT_HINT_LEN + 2u +
           HERMES_SIGNATURE_LEN + 4u + 8u;
}

static hermes_status hermes_build_core_preimage(const hermes_envelope *env, uint8_t **out, size_t *out_len) {
    uint8_t *buffer;
    size_t len;
    size_t off = 0;
    if (!env || !out || !out_len) {
        return HERMES_ERR_ARGUMENT;
    }
    len = 1u + 1u + 1u + 2u + 8u + 8u + HERMES_PUBLIC_ID_LEN + HERMES_RECIPIENT_HINT_LEN + 2u +
          env->payload_size + 4u + 2u + env->reserved_len;
    buffer = (uint8_t *) malloc(len);
    if (!buffer) {
        return HERMES_ERR_MEMORY;
    }
    buffer[off++] = env->protocol_version;
    buffer[off++] = env->crypto_suite_id;
    buffer[off++] = env->pow_algorithm;
    hermes_write_be16(buffer + off, env->flags);
    off += 2u;
    hermes_write_be64(buffer + off, env->created_at_unix);
    off += 8u;
    hermes_write_be64(buffer + off, env->expires_at_unix);
    off += 8u;
    memcpy(buffer + off, env->sender_public_identity.sig_public, HERMES_SIG_PUBLIC_LEN);
    off += HERMES_SIG_PUBLIC_LEN;
    memcpy(buffer + off, env->sender_public_identity.box_public, HERMES_BOX_PUBLIC_LEN);
    off += HERMES_BOX_PUBLIC_LEN;
    memcpy(buffer + off, env->recipient_hint, HERMES_RECIPIENT_HINT_LEN);
    off += HERMES_RECIPIENT_HINT_LEN;
    hermes_write_be16(buffer + off, env->payload_size);
    off += 2u;
    memcpy(buffer + off, env->encrypted_payload, env->payload_size);
    off += env->payload_size;
    hermes_write_be32(buffer + off, env->pow_difficulty);
    off += 4u;
    hermes_write_be16(buffer + off, env->reserved_len);
    off += 2u;
    memcpy(buffer + off, env->reserved, env->reserved_len);
    off += env->reserved_len;
    if (off != len) {
        free(buffer);
        return HERMES_ERR_PROTOCOL;
    }
    *out = buffer;
    *out_len = len;
    return HERMES_OK;
}

static hermes_status hermes_build_sign_preimage(const hermes_envelope *env, uint8_t **out, size_t *out_len) {
    uint8_t *buffer;
    size_t len;
    size_t off = 0;
    if (!env || !out || !out_len) {
        return HERMES_ERR_ARGUMENT;
    }
    len = 4u + 1u + 1u + 1u + 1u + 2u + 2u + HERMES_ID_LEN + 8u + 8u + HERMES_PUBLIC_ID_LEN +
          HERMES_RECIPIENT_HINT_LEN + 2u + env->payload_size + 4u + env->reserved_len;
    buffer = (uint8_t *) malloc(len);
    if (!buffer) {
        return HERMES_ERR_MEMORY;
    }
    memcpy(buffer + off, "HRM1", 4u);
    off += 4u;
    buffer[off++] = env->protocol_version;
    buffer[off++] = env->crypto_suite_id;
    buffer[off++] = env->pow_algorithm;
    buffer[off++] = env->reserved_header;
    hermes_write_be16(buffer + off, env->flags);
    off += 2u;
    hermes_write_be16(buffer + off, env->reserved_len);
    off += 2u;
    memcpy(buffer + off, env->envelope_id, HERMES_ID_LEN);
    off += HERMES_ID_LEN;
    hermes_write_be64(buffer + off, env->created_at_unix);
    off += 8u;
    hermes_write_be64(buffer + off, env->expires_at_unix);
    off += 8u;
    memcpy(buffer + off, env->sender_public_identity.sig_public, HERMES_SIG_PUBLIC_LEN);
    off += HERMES_SIG_PUBLIC_LEN;
    memcpy(buffer + off, env->sender_public_identity.box_public, HERMES_BOX_PUBLIC_LEN);
    off += HERMES_BOX_PUBLIC_LEN;
    memcpy(buffer + off, env->recipient_hint, HERMES_RECIPIENT_HINT_LEN);
    off += HERMES_RECIPIENT_HINT_LEN;
    hermes_write_be16(buffer + off, env->payload_size);
    off += 2u;
    memcpy(buffer + off, env->encrypted_payload, env->payload_size);
    off += env->payload_size;
    hermes_write_be32(buffer + off, env->pow_difficulty);
    off += 4u;
    memcpy(buffer + off, env->reserved, env->reserved_len);
    off += env->reserved_len;
    if (off != len) {
        free(buffer);
        return HERMES_ERR_PROTOCOL;
    }
    *out = buffer;
    *out_len = len;
    return HERMES_OK;
}

void hermes_envelope_init(hermes_envelope *env) {
    if (!env) {
        return;
    }
    memset(env, 0, sizeof(*env));
    env->protocol_version = HERMES_PROTOCOL_VERSION;
    env->crypto_suite_id = HERMES_CRYPTO_SUITE_V1;
    env->pow_algorithm = HERMES_POW_ALG_SHA256_LZ;
}

static uint64_t hermes_default_pow_attempt_budget(uint32_t pow_difficulty) {
    uint32_t exponent = pow_difficulty + 2u;
    if (exponent < 24u) {
        exponent = 24u;
    }
    if (exponent > 40u) {
        exponent = 40u;
    }
    return 1ull << exponent;
}

hermes_status hermes_envelope_compute_id(hermes_envelope *env) {
    uint8_t *preimage = NULL;
    size_t preimage_len = 0;
    uint8_t digest[HERMES_HASH_LEN];
    uint8_t *buffer = NULL;
    size_t buffer_len = 10u + preimage_len;
    hermes_status status;
    if (!env) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_build_core_preimage(env, &preimage, &preimage_len);
    if (status != HERMES_OK) {
        return status;
    }
    buffer_len = 10u + preimage_len;
    buffer = (uint8_t *) malloc(buffer_len);
    if (!buffer) {
        free(preimage);
        return HERMES_ERR_MEMORY;
    }
    memcpy(buffer, "HRM-EID-V1", 10u);
    memcpy(buffer + 10u, preimage, preimage_len);
    status = hermes_sha256(buffer, buffer_len, digest);
    if (status == HERMES_OK) {
        memcpy(env->envelope_id, digest, HERMES_ID_LEN);
    }
    free(buffer);
    free(preimage);
    return status;
}

hermes_status hermes_envelope_sign(hermes_envelope *env, const hermes_identity *sender) {
    uint8_t *preimage = NULL;
    size_t preimage_len = 0;
    hermes_status status;
    if (!env || !sender) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_build_sign_preimage(env, &preimage, &preimage_len);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_sign_detached(sender, preimage, preimage_len, env->signature);
    free(preimage);
    return status;
}

hermes_status hermes_envelope_create(hermes_envelope *env,
                                     const hermes_identity *sender,
                                     const hermes_contact *recipient,
                                     const uint8_t *plaintext,
                                     size_t plaintext_len,
                                     uint64_t created_at_unix,
                                     uint64_t ttl_seconds,
                                     uint32_t flags,
                                     uint32_t pow_difficulty) {
    hermes_status status;
    size_t ciphertext_len = 0;
    if (!env || !sender || !recipient || (!plaintext && plaintext_len > 0)) {
        return HERMES_ERR_ARGUMENT;
    }
    if (ttl_seconds == 0) {
        ttl_seconds = HERMES_DEFAULT_TTL_SECONDS;
    }
    if (ttl_seconds > HERMES_MAX_TTL_SECONDS) {
        return HERMES_ERR_RANGE;
    }
    hermes_envelope_init(env);
    env->flags = (uint16_t) flags;
    env->created_at_unix = created_at_unix;
    env->expires_at_unix = created_at_unix + ttl_seconds;
    env->pow_difficulty = pow_difficulty;
    memcpy(&env->sender_public_identity, &sender->public_identity, sizeof(env->sender_public_identity));
    memcpy(env->recipient_hint, recipient->recipient_hint, HERMES_RECIPIENT_HINT_LEN);

    status = hermes_encrypt_payload(recipient,
                                    env,
                                    plaintext,
                                    plaintext_len,
                                    env->encrypted_payload,
                                    &ciphertext_len);
    if (status != HERMES_OK) {
        return status;
    }
    if (ciphertext_len > HERMES_MAX_ENCRYPTED_PAYLOAD) {
        return HERMES_ERR_RANGE;
    }
    env->payload_size = (uint16_t) ciphertext_len;
    status = hermes_envelope_compute_id(env);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_envelope_sign(env, sender);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_pow_solve(env, hermes_default_pow_attempt_budget(env->pow_difficulty));
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_envelope_encode(env, NULL, &env->encoded_size);
}

hermes_status hermes_envelope_encode(const hermes_envelope *env, uint8_t *out, size_t *out_len) {
    size_t needed;
    size_t off = 0;
    if (!env || !out_len) {
        return HERMES_ERR_ARGUMENT;
    }
    if (env->payload_size > HERMES_MAX_ENCRYPTED_PAYLOAD || env->reserved_len > HERMES_MAX_RESERVED) {
        return HERMES_ERR_RANGE;
    }
    needed = hermes_envelope_min_size() + env->payload_size + env->reserved_len;
    if (needed > HERMES_MAX_ENVELOPE_SIZE) {
        return HERMES_ERR_RANGE;
    }
    if (!out) {
        *out_len = needed;
        return HERMES_OK;
    }
    if (*out_len < needed) {
        return HERMES_ERR_RANGE;
    }
    memcpy(out + off, "HRM1", 4u);
    off += 4u;
    out[off++] = env->protocol_version;
    out[off++] = env->crypto_suite_id;
    out[off++] = env->pow_algorithm;
    out[off++] = env->reserved_header;
    hermes_write_be16(out + off, env->flags);
    off += 2u;
    hermes_write_be16(out + off, env->reserved_len);
    off += 2u;
    memcpy(out + off, env->envelope_id, HERMES_ID_LEN);
    off += HERMES_ID_LEN;
    hermes_write_be64(out + off, env->created_at_unix);
    off += 8u;
    hermes_write_be64(out + off, env->expires_at_unix);
    off += 8u;
    memcpy(out + off, env->sender_public_identity.sig_public, HERMES_SIG_PUBLIC_LEN);
    off += HERMES_SIG_PUBLIC_LEN;
    memcpy(out + off, env->sender_public_identity.box_public, HERMES_BOX_PUBLIC_LEN);
    off += HERMES_BOX_PUBLIC_LEN;
    memcpy(out + off, env->recipient_hint, HERMES_RECIPIENT_HINT_LEN);
    off += HERMES_RECIPIENT_HINT_LEN;
    hermes_write_be16(out + off, env->payload_size);
    off += 2u;
    memcpy(out + off, env->encrypted_payload, env->payload_size);
    off += env->payload_size;
    memcpy(out + off, env->signature, HERMES_SIGNATURE_LEN);
    off += HERMES_SIGNATURE_LEN;
    hermes_write_be32(out + off, env->pow_difficulty);
    off += 4u;
    hermes_write_be64(out + off, env->pow_nonce);
    off += 8u;
    memcpy(out + off, env->reserved, env->reserved_len);
    off += env->reserved_len;
    if (off != needed) {
        return HERMES_ERR_PROTOCOL;
    }
    *out_len = needed;
    return HERMES_OK;
}

hermes_status hermes_envelope_decode(const uint8_t *data, size_t data_len, hermes_envelope *env) {
    size_t off = 0;
    size_t min_size = hermes_envelope_min_size();
    if (!data || !env) {
        return HERMES_ERR_ARGUMENT;
    }
    if (data_len < min_size || data_len > HERMES_MAX_ENVELOPE_SIZE) {
        return HERMES_ERR_FORMAT;
    }
    if (memcmp(data, "HRM1", 4u) != 0) {
        return HERMES_ERR_FORMAT;
    }
    hermes_envelope_init(env);
    off += 4u;
    env->protocol_version = data[off++];
    env->crypto_suite_id = data[off++];
    env->pow_algorithm = data[off++];
    env->reserved_header = data[off++];
    env->flags = hermes_read_be16(data + off);
    off += 2u;
    env->reserved_len = hermes_read_be16(data + off);
    off += 2u;
    if (env->reserved_len > HERMES_MAX_RESERVED) {
        return HERMES_ERR_RANGE;
    }
    memcpy(env->envelope_id, data + off, HERMES_ID_LEN);
    off += HERMES_ID_LEN;
    env->created_at_unix = hermes_read_be64(data + off);
    off += 8u;
    env->expires_at_unix = hermes_read_be64(data + off);
    off += 8u;
    memcpy(env->sender_public_identity.sig_public, data + off, HERMES_SIG_PUBLIC_LEN);
    off += HERMES_SIG_PUBLIC_LEN;
    memcpy(env->sender_public_identity.box_public, data + off, HERMES_BOX_PUBLIC_LEN);
    off += HERMES_BOX_PUBLIC_LEN;
    memcpy(env->recipient_hint, data + off, HERMES_RECIPIENT_HINT_LEN);
    off += HERMES_RECIPIENT_HINT_LEN;
    env->payload_size = hermes_read_be16(data + off);
    off += 2u;
    if (env->payload_size > HERMES_MAX_ENCRYPTED_PAYLOAD) {
        return HERMES_ERR_RANGE;
    }
    if (data_len != min_size + env->payload_size + env->reserved_len) {
        return HERMES_ERR_FORMAT;
    }
    memcpy(env->encrypted_payload, data + off, env->payload_size);
    off += env->payload_size;
    memcpy(env->signature, data + off, HERMES_SIGNATURE_LEN);
    off += HERMES_SIGNATURE_LEN;
    env->pow_difficulty = hermes_read_be32(data + off);
    off += 4u;
    env->pow_nonce = hermes_read_be64(data + off);
    off += 8u;
    memcpy(env->reserved, data + off, env->reserved_len);
    off += env->reserved_len;
    if (off != data_len) {
        return HERMES_ERR_FORMAT;
    }
    env->encoded_size = data_len;
    return HERMES_OK;
}

int hermes_envelope_is_expired(const hermes_envelope *env, uint64_t now_unix) {
    if (!env) {
        return 1;
    }
    return now_unix > env->expires_at_unix;
}

hermes_status hermes_envelope_hash_wire(const hermes_envelope *env, uint8_t out[HERMES_HASH_LEN]) {
    uint8_t buffer[HERMES_MAX_ENVELOPE_SIZE];
    size_t len = sizeof(buffer);
    hermes_status status;
    if (!env || !out) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_envelope_encode(env, buffer, &len);
    if (status != HERMES_OK) {
        return status;
    }
    return hermes_sha256(buffer, len, out);
}

hermes_status hermes_envelope_verify(const hermes_envelope *env, uint64_t now_unix) {
    hermes_envelope copy;
    uint8_t *preimage = NULL;
    size_t preimage_len = 0;
    uint8_t wire_hash[HERMES_HASH_LEN];
    hermes_status status;

    if (!env) {
        return HERMES_ERR_ARGUMENT;
    }
    if (env->protocol_version != HERMES_PROTOCOL_VERSION ||
        env->crypto_suite_id != HERMES_CRYPTO_SUITE_V1 ||
        env->pow_algorithm != HERMES_POW_ALG_SHA256_LZ) {
        return HERMES_ERR_PROTOCOL;
    }
    if (env->expires_at_unix < env->created_at_unix) {
        return HERMES_ERR_RANGE;
    }
    if ((env->expires_at_unix - env->created_at_unix) > HERMES_MAX_TTL_SECONDS) {
        return HERMES_ERR_RANGE;
    }
    if (now_unix != 0 && hermes_envelope_is_expired(env, now_unix)) {
        return HERMES_ERR_EXPIRED;
    }
    memcpy(&copy, env, sizeof(copy));
    status = hermes_envelope_compute_id(&copy);
    if (status != HERMES_OK) {
        return status;
    }
    if (!hermes_ct_equal(copy.envelope_id, env->envelope_id, HERMES_ID_LEN)) {
        return HERMES_ERR_VERIFY;
    }
    status = hermes_build_sign_preimage(env, &preimage, &preimage_len);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_verify_detached(&env->sender_public_identity, preimage, preimage_len, env->signature);
    free(preimage);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_pow_verify(env);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_envelope_hash_wire(env, wire_hash);
    if (status != HERMES_OK) {
        return status;
    }
    (void) wire_hash;
    return HERMES_OK;
}
