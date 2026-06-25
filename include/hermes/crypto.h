#ifndef HERMES_CRYPTO_H
#define HERMES_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"
#include "types.h"

hermes_status hermes_crypto_init(void);
void hermes_crypto_cleanup(void);

hermes_status hermes_random_bytes(uint8_t *dst, size_t len);
hermes_status hermes_sha256(const uint8_t *data, size_t len, uint8_t out[HERMES_HASH_LEN]);

hermes_status hermes_identity_generate(const char *alias, hermes_identity *identity);
hermes_status hermes_contact_from_identity(const hermes_identity *identity, hermes_contact *contact);
hermes_status hermes_contact_compute_hint(const hermes_public_identity *public_identity,
                                          uint8_t hint[HERMES_RECIPIENT_HINT_LEN]);
hermes_status hermes_contact_fingerprint(const hermes_public_identity *public_identity,
                                         uint8_t fingerprint[HERMES_HASH_LEN]);

hermes_status hermes_sign_detached(const hermes_identity *identity,
                                   const uint8_t *msg,
                                   size_t msg_len,
                                   uint8_t sig[HERMES_SIGNATURE_LEN]);

hermes_status hermes_verify_detached(const hermes_public_identity *identity,
                                     const uint8_t *msg,
                                     size_t msg_len,
                                     const uint8_t sig[HERMES_SIGNATURE_LEN]);

hermes_status hermes_encrypt_payload(const hermes_contact *recipient,
                                     const hermes_envelope *context,
                                     const uint8_t *plaintext,
                                     size_t plaintext_len,
                                     uint8_t *ciphertext,
                                     size_t *ciphertext_len);

hermes_status hermes_decrypt_payload(const hermes_identity *recipient,
                                     const hermes_envelope *context,
                                     const uint8_t *ciphertext,
                                     size_t ciphertext_len,
                                     uint8_t *plaintext,
                                     size_t *plaintext_len);

hermes_status hermes_pow_hash(const hermes_envelope *env, uint64_t nonce, uint8_t out[HERMES_HASH_LEN]);
int hermes_pow_meets_difficulty(const uint8_t hash[HERMES_HASH_LEN], uint32_t difficulty);
hermes_status hermes_pow_solve(hermes_envelope *env, uint64_t max_attempts);
hermes_status hermes_pow_verify(const hermes_envelope *env);

#endif

