#ifndef HERMES_VERSION_H
#define HERMES_VERSION_H

#define HERMES_PROTOCOL_VERSION 1u
#define HERMES_CRYPTO_SUITE_V1 1u
#define HERMES_POW_ALG_SHA256_LZ 1u

#define HERMES_ID_LEN 16u
#define HERMES_SIG_PUBLIC_LEN 32u
#define HERMES_SIG_PRIVATE_LEN 32u
#define HERMES_BOX_PUBLIC_LEN 32u
#define HERMES_BOX_PRIVATE_LEN 32u
#define HERMES_PUBLIC_ID_LEN 64u
#define HERMES_RECIPIENT_HINT_LEN 20u
#define HERMES_SIGNATURE_LEN 64u
#define HERMES_HASH_LEN 32u

#define HERMES_MAX_PLAINTEXT 1024u
#define HERMES_MAX_ENCRYPTED_PAYLOAD 1536u
#define HERMES_MAX_RESERVED 256u
#define HERMES_MAX_ENVELOPE_SIZE 2048u

#define HERMES_DEFAULT_TTL_SECONDS (10u * 24u * 60u * 60u)
#define HERMES_MAX_TTL_SECONDS (10u * 24u * 60u * 60u)
#define HERMES_DEDUP_RETENTION_SECONDS (14u * 24u * 60u * 60u)

_Static_assert(HERMES_SIG_PUBLIC_LEN == 32u, "unexpected Ed25519 public key size");
_Static_assert(HERMES_BOX_PUBLIC_LEN == 32u, "unexpected X25519 public key size");
_Static_assert(HERMES_SIGNATURE_LEN == 64u, "unexpected Ed25519 signature size");
_Static_assert(HERMES_MAX_ENVELOPE_SIZE <= 4096u, "envelope size guard drifted");

#endif
