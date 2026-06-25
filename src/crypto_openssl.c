#include "hermes/crypto.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <string.h>

#include "hermes/envelope.h"
#include "hermes/util.h"

#define HERMES_AEAD_NONCE_LEN 12u
#define HERMES_AEAD_TAG_LEN 16u
#define HERMES_EPHEMERAL_LEN 32u
#define HERMES_ENC_INFO "HermesRelay-v1-enc"

static hermes_status hermes_make_raw_private_pkey(int type, const uint8_t *raw, size_t raw_len, EVP_PKEY **out) {
    EVP_PKEY *pkey;
    if (!raw || !out) {
        return HERMES_ERR_ARGUMENT;
    }
    pkey = EVP_PKEY_new_raw_private_key(type, NULL, raw, raw_len);
    if (!pkey) {
        return HERMES_ERR_CRYPTO;
    }
    *out = pkey;
    return HERMES_OK;
}

static hermes_status hermes_make_raw_public_pkey(int type, const uint8_t *raw, size_t raw_len, EVP_PKEY **out) {
    EVP_PKEY *pkey;
    if (!raw || !out) {
        return HERMES_ERR_ARGUMENT;
    }
    pkey = EVP_PKEY_new_raw_public_key(type, NULL, raw, raw_len);
    if (!pkey) {
        return HERMES_ERR_CRYPTO;
    }
    *out = pkey;
    return HERMES_OK;
}

static hermes_status hermes_build_aead_ad(const hermes_envelope *context,
                                          uint8_t *ad,
                                          size_t *ad_len) {
    size_t needed = 9u + 1u + 1u + 2u + 8u + 8u + HERMES_PUBLIC_ID_LEN + HERMES_RECIPIENT_HINT_LEN;
    size_t offset = 0;
    if (!context || !ad || !ad_len) {
        return HERMES_ERR_ARGUMENT;
    }
    memcpy(ad + offset, "HRM-AD-V1", 9u);
    offset += 9u;
    ad[offset++] = context->protocol_version;
    ad[offset++] = context->crypto_suite_id;
    hermes_write_be16(ad + offset, context->flags);
    offset += 2u;
    hermes_write_be64(ad + offset, context->created_at_unix);
    offset += 8u;
    hermes_write_be64(ad + offset, context->expires_at_unix);
    offset += 8u;
    memcpy(ad + offset, context->sender_public_identity.sig_public, HERMES_SIG_PUBLIC_LEN);
    offset += HERMES_SIG_PUBLIC_LEN;
    memcpy(ad + offset, context->sender_public_identity.box_public, HERMES_BOX_PUBLIC_LEN);
    offset += HERMES_BOX_PUBLIC_LEN;
    memcpy(ad + offset, context->recipient_hint, HERMES_RECIPIENT_HINT_LEN);
    offset += HERMES_RECIPIENT_HINT_LEN;
    if (offset != needed) {
        return HERMES_ERR_PROTOCOL;
    }
    *ad_len = offset;
    return HERMES_OK;
}

static hermes_status hermes_hkdf_derive(const uint8_t *secret,
                                        size_t secret_len,
                                        const uint8_t *salt,
                                        size_t salt_len,
                                        uint8_t *out,
                                        size_t out_len) {
    EVP_PKEY_CTX *ctx;
    size_t produced = out_len;
    if (!secret || !out) {
        return HERMES_ERR_ARGUMENT;
    }
    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
    if (!ctx) {
        return HERMES_ERR_CRYPTO;
    }
    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(ctx, salt, (int) salt_len) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(ctx, secret, (int) secret_len) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(ctx, (const unsigned char *) HERMES_ENC_INFO, (int) (sizeof(HERMES_ENC_INFO) - 1u)) <= 0 ||
        EVP_PKEY_derive(ctx, out, &produced) <= 0 ||
        produced != out_len) {
        EVP_PKEY_CTX_free(ctx);
        return HERMES_ERR_CRYPTO;
    }
    EVP_PKEY_CTX_free(ctx);
    return HERMES_OK;
}

static hermes_status hermes_x25519_shared_secret(const uint8_t private_key[HERMES_BOX_PRIVATE_LEN],
                                                 const uint8_t public_key[HERMES_BOX_PUBLIC_LEN],
                                                 uint8_t out[HERMES_HASH_LEN]) {
    EVP_PKEY *priv = NULL;
    EVP_PKEY *pub = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    size_t out_len = HERMES_HASH_LEN;
    hermes_status status;

    status = hermes_make_raw_private_pkey(EVP_PKEY_X25519, private_key, HERMES_BOX_PRIVATE_LEN, &priv);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_make_raw_public_pkey(EVP_PKEY_X25519, public_key, HERMES_BOX_PUBLIC_LEN, &pub);
    if (status != HERMES_OK) {
        EVP_PKEY_free(priv);
        return status;
    }
    ctx = EVP_PKEY_CTX_new(priv, NULL);
    if (!ctx) {
        EVP_PKEY_free(priv);
        EVP_PKEY_free(pub);
        return HERMES_ERR_CRYPTO;
    }
    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_derive_set_peer(ctx, pub) <= 0 ||
        EVP_PKEY_derive(ctx, out, &out_len) <= 0 ||
        out_len != HERMES_HASH_LEN) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(priv);
        EVP_PKEY_free(pub);
        return HERMES_ERR_CRYPTO;
    }
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(pub);
    return HERMES_OK;
}

hermes_status hermes_crypto_init(void) {
    if (OPENSSL_init_crypto(0, NULL) != 1) {
        return HERMES_ERR_CRYPTO;
    }
    return HERMES_OK;
}

void hermes_crypto_cleanup(void) {
}

hermes_status hermes_random_bytes(uint8_t *dst, size_t len) {
    if (!dst) {
        return HERMES_ERR_ARGUMENT;
    }
    if (RAND_bytes(dst, (int) len) != 1) {
        return HERMES_ERR_CRYPTO;
    }
    return HERMES_OK;
}

hermes_status hermes_sha256(const uint8_t *data, size_t len, uint8_t out[HERMES_HASH_LEN]) {
    EVP_MD_CTX *ctx;
    unsigned int out_len = HERMES_HASH_LEN;
    if ((!data && len > 0) || !out) {
        return HERMES_ERR_ARGUMENT;
    }
    ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return HERMES_ERR_CRYPTO;
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        (len > 0 && EVP_DigestUpdate(ctx, data, len) != 1) ||
        EVP_DigestFinal_ex(ctx, out, &out_len) != 1 ||
        out_len != HERMES_HASH_LEN) {
        EVP_MD_CTX_free(ctx);
        return HERMES_ERR_CRYPTO;
    }
    EVP_MD_CTX_free(ctx);
    return HERMES_OK;
}

hermes_status hermes_identity_generate(const char *alias, hermes_identity *identity) {
    EVP_PKEY_CTX *ctx = NULL;
    EVP_PKEY *sig = NULL;
    EVP_PKEY *box = NULL;
    size_t len;
    if (!identity) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(identity, 0, sizeof(*identity));
    if (alias) {
        size_t alias_len = strlen(alias);
        if (alias_len >= sizeof(identity->alias)) {
            return HERMES_ERR_RANGE;
        }
        memcpy(identity->alias, alias, alias_len + 1u);
    } else {
        memcpy(identity->alias, "unnamed", 8u);
    }

    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_keygen(ctx, &sig) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return HERMES_ERR_CRYPTO;
    }
    EVP_PKEY_CTX_free(ctx);

    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_keygen(ctx, &box) <= 0) {
        EVP_PKEY_free(sig);
        EVP_PKEY_CTX_free(ctx);
        return HERMES_ERR_CRYPTO;
    }
    EVP_PKEY_CTX_free(ctx);

    len = HERMES_SIG_PUBLIC_LEN;
    if (EVP_PKEY_get_raw_public_key(sig, identity->public_identity.sig_public, &len) <= 0 || len != HERMES_SIG_PUBLIC_LEN) {
        EVP_PKEY_free(sig);
        EVP_PKEY_free(box);
        return HERMES_ERR_CRYPTO;
    }
    len = HERMES_SIG_PRIVATE_LEN;
    if (EVP_PKEY_get_raw_private_key(sig, identity->sig_private, &len) <= 0 || len != HERMES_SIG_PRIVATE_LEN) {
        EVP_PKEY_free(sig);
        EVP_PKEY_free(box);
        return HERMES_ERR_CRYPTO;
    }
    len = HERMES_BOX_PUBLIC_LEN;
    if (EVP_PKEY_get_raw_public_key(box, identity->public_identity.box_public, &len) <= 0 || len != HERMES_BOX_PUBLIC_LEN) {
        EVP_PKEY_free(sig);
        EVP_PKEY_free(box);
        return HERMES_ERR_CRYPTO;
    }
    len = HERMES_BOX_PRIVATE_LEN;
    if (EVP_PKEY_get_raw_private_key(box, identity->box_private, &len) <= 0 || len != HERMES_BOX_PRIVATE_LEN) {
        EVP_PKEY_free(sig);
        EVP_PKEY_free(box);
        return HERMES_ERR_CRYPTO;
    }

    EVP_PKEY_free(sig);
    EVP_PKEY_free(box);
    return HERMES_OK;
}

hermes_status hermes_contact_compute_hint(const hermes_public_identity *public_identity,
                                          uint8_t hint[HERMES_RECIPIENT_HINT_LEN]) {
    uint8_t digest[HERMES_HASH_LEN];
    uint8_t material[HERMES_PUBLIC_ID_LEN];
    if (!public_identity || !hint) {
        return HERMES_ERR_ARGUMENT;
    }
    memcpy(material, public_identity->sig_public, HERMES_SIG_PUBLIC_LEN);
    memcpy(material + HERMES_SIG_PUBLIC_LEN, public_identity->box_public, HERMES_BOX_PUBLIC_LEN);
    if (hermes_sha256(material, sizeof(material), digest) != HERMES_OK) {
        return HERMES_ERR_CRYPTO;
    }
    memcpy(hint, digest, HERMES_RECIPIENT_HINT_LEN);
    return HERMES_OK;
}

hermes_status hermes_contact_fingerprint(const hermes_public_identity *public_identity,
                                         uint8_t fingerprint[HERMES_HASH_LEN]) {
    uint8_t material[HERMES_PUBLIC_ID_LEN];
    if (!public_identity || !fingerprint) {
        return HERMES_ERR_ARGUMENT;
    }
    memcpy(material, public_identity->sig_public, HERMES_SIG_PUBLIC_LEN);
    memcpy(material + HERMES_SIG_PUBLIC_LEN, public_identity->box_public, HERMES_BOX_PUBLIC_LEN);
    return hermes_sha256(material, sizeof(material), fingerprint);
}

hermes_status hermes_contact_from_identity(const hermes_identity *identity, hermes_contact *contact) {
    if (!identity || !contact) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(contact, 0, sizeof(*contact));
    memcpy(contact->alias, identity->alias, sizeof(contact->alias));
    memcpy(&contact->public_identity, &identity->public_identity, sizeof(contact->public_identity));
    if (hermes_contact_compute_hint(&identity->public_identity, contact->recipient_hint) != HERMES_OK) {
        return HERMES_ERR_CRYPTO;
    }
    if (hermes_contact_fingerprint(&identity->public_identity, contact->fingerprint) != HERMES_OK) {
        return HERMES_ERR_CRYPTO;
    }
    return HERMES_OK;
}

hermes_status hermes_sign_detached(const hermes_identity *identity,
                                   const uint8_t *msg,
                                   size_t msg_len,
                                   uint8_t sig[HERMES_SIGNATURE_LEN]) {
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *ctx = NULL;
    size_t sig_len = HERMES_SIGNATURE_LEN;
    hermes_status status;
    if (!identity || (!msg && msg_len > 0) || !sig) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_make_raw_private_pkey(EVP_PKEY_ED25519, identity->sig_private, HERMES_SIG_PRIVATE_LEN, &pkey);
    if (status != HERMES_OK) {
        return status;
    }
    ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return HERMES_ERR_CRYPTO;
    }
    if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pkey) <= 0 ||
        EVP_DigestSign(ctx, sig, &sig_len, msg, msg_len) <= 0 ||
        sig_len != HERMES_SIGNATURE_LEN) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return HERMES_ERR_CRYPTO;
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return HERMES_OK;
}

hermes_status hermes_verify_detached(const hermes_public_identity *identity,
                                     const uint8_t *msg,
                                     size_t msg_len,
                                     const uint8_t sig[HERMES_SIGNATURE_LEN]) {
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *ctx = NULL;
    hermes_status status;
    if (!identity || (!msg && msg_len > 0) || !sig) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_make_raw_public_pkey(EVP_PKEY_ED25519, identity->sig_public, HERMES_SIG_PUBLIC_LEN, &pkey);
    if (status != HERMES_OK) {
        return status;
    }
    ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return HERMES_ERR_CRYPTO;
    }
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) <= 0 ||
        EVP_DigestVerify(ctx, sig, HERMES_SIGNATURE_LEN, msg, msg_len) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return HERMES_ERR_VERIFY;
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return HERMES_OK;
}

hermes_status hermes_encrypt_payload(const hermes_contact *recipient,
                                     const hermes_envelope *context,
                                     const uint8_t *plaintext,
                                     size_t plaintext_len,
                                     uint8_t *ciphertext,
                                     size_t *ciphertext_len) {
    EVP_PKEY_CTX *kctx = NULL;
    EVP_PKEY *ephemeral = NULL;
    EVP_CIPHER_CTX *cipher = NULL;
    uint8_t epk_pub[HERMES_EPHEMERAL_LEN];
    uint8_t epk_priv[HERMES_BOX_PRIVATE_LEN];
    uint8_t shared[HERMES_HASH_LEN];
    uint8_t derived[32u + HERMES_AEAD_NONCE_LEN];
    uint8_t ad[256];
    uint8_t salt[HERMES_HASH_LEN];
    size_t ad_len = 0;
    size_t out_len;
    size_t raw_len;
    int chunk_len = 0;
    int final_len = 0;
    hermes_status status;

    if (!recipient || !context || (!plaintext && plaintext_len > 0) || !ciphertext || !ciphertext_len) {
        return HERMES_ERR_ARGUMENT;
    }
    if (plaintext_len > HERMES_MAX_PLAINTEXT) {
        return HERMES_ERR_RANGE;
    }
    out_len = HERMES_EPHEMERAL_LEN + 2u + plaintext_len + HERMES_AEAD_TAG_LEN;
    if (out_len > HERMES_MAX_ENCRYPTED_PAYLOAD) {
        return HERMES_ERR_RANGE;
    }

    kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!kctx || EVP_PKEY_keygen_init(kctx) <= 0 || EVP_PKEY_keygen(kctx, &ephemeral) <= 0) {
        EVP_PKEY_CTX_free(kctx);
        return HERMES_ERR_CRYPTO;
    }
    EVP_PKEY_CTX_free(kctx);

    raw_len = HERMES_EPHEMERAL_LEN;
    if (EVP_PKEY_get_raw_public_key(ephemeral, epk_pub, &raw_len) <= 0 || raw_len != HERMES_EPHEMERAL_LEN) {
        EVP_PKEY_free(ephemeral);
        return HERMES_ERR_CRYPTO;
    }
    raw_len = HERMES_BOX_PRIVATE_LEN;
    if (EVP_PKEY_get_raw_private_key(ephemeral, epk_priv, &raw_len) <= 0 || raw_len != HERMES_BOX_PRIVATE_LEN) {
        EVP_PKEY_free(ephemeral);
        return HERMES_ERR_CRYPTO;
    }
    EVP_PKEY_free(ephemeral);

    status = hermes_x25519_shared_secret(epk_priv, recipient->public_identity.box_public, shared);
    if (status != HERMES_OK) {
        hermes_secure_bzero(epk_priv, sizeof(epk_priv));
        return status;
    }
    if (hermes_build_aead_ad(context, ad, &ad_len) != HERMES_OK) {
        hermes_secure_bzero(epk_priv, sizeof(epk_priv));
        hermes_secure_bzero(shared, sizeof(shared));
        return HERMES_ERR_PROTOCOL;
    }
    if (hermes_sha256(ad, ad_len, salt) != HERMES_OK) {
        hermes_secure_bzero(epk_priv, sizeof(epk_priv));
        hermes_secure_bzero(shared, sizeof(shared));
        return HERMES_ERR_CRYPTO;
    }
    if (hermes_hkdf_derive(shared, sizeof(shared), salt, sizeof(salt), derived, sizeof(derived)) != HERMES_OK) {
        hermes_secure_bzero(epk_priv, sizeof(epk_priv));
        hermes_secure_bzero(shared, sizeof(shared));
        return HERMES_ERR_CRYPTO;
    }

    memcpy(ciphertext, epk_pub, HERMES_EPHEMERAL_LEN);
    hermes_write_be16(ciphertext + HERMES_EPHEMERAL_LEN, (uint16_t) plaintext_len);

    cipher = EVP_CIPHER_CTX_new();
    if (!cipher) {
        hermes_secure_bzero(epk_priv, sizeof(epk_priv));
        hermes_secure_bzero(shared, sizeof(shared));
        hermes_secure_bzero(derived, sizeof(derived));
        return HERMES_ERR_CRYPTO;
    }
    if (EVP_EncryptInit_ex(cipher, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_AEAD_SET_IVLEN, HERMES_AEAD_NONCE_LEN, NULL) != 1 ||
        EVP_EncryptInit_ex(cipher, NULL, NULL, derived, derived + 32u) != 1 ||
        EVP_EncryptUpdate(cipher, NULL, &chunk_len, ad, (int) ad_len) != 1 ||
        EVP_EncryptUpdate(cipher,
                          ciphertext + HERMES_EPHEMERAL_LEN + 2u,
                          &chunk_len,
                          plaintext,
                          (int) plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(cipher);
        hermes_secure_bzero(epk_priv, sizeof(epk_priv));
        hermes_secure_bzero(shared, sizeof(shared));
        hermes_secure_bzero(derived, sizeof(derived));
        return HERMES_ERR_CRYPTO;
    }
    if (EVP_EncryptFinal_ex(cipher, ciphertext + HERMES_EPHEMERAL_LEN + 2u + chunk_len, &final_len) != 1 ||
        EVP_CIPHER_CTX_ctrl(cipher,
                            EVP_CTRL_AEAD_GET_TAG,
                            HERMES_AEAD_TAG_LEN,
                            ciphertext + HERMES_EPHEMERAL_LEN + 2u + plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(cipher);
        hermes_secure_bzero(epk_priv, sizeof(epk_priv));
        hermes_secure_bzero(shared, sizeof(shared));
        hermes_secure_bzero(derived, sizeof(derived));
        return HERMES_ERR_CRYPTO;
    }
    EVP_CIPHER_CTX_free(cipher);

    *ciphertext_len = out_len;
    hermes_secure_bzero(epk_priv, sizeof(epk_priv));
    hermes_secure_bzero(shared, sizeof(shared));
    hermes_secure_bzero(derived, sizeof(derived));
    return HERMES_OK;
}

hermes_status hermes_decrypt_payload(const hermes_identity *recipient,
                                     const hermes_envelope *context,
                                     const uint8_t *ciphertext,
                                     size_t ciphertext_len,
                                     uint8_t *plaintext,
                                     size_t *plaintext_len) {
    EVP_CIPHER_CTX *cipher = NULL;
    uint8_t shared[HERMES_HASH_LEN];
    uint8_t derived[32u + HERMES_AEAD_NONCE_LEN];
    uint8_t ad[256];
    uint8_t salt[HERMES_HASH_LEN];
    uint16_t pt_len;
    size_t ad_len = 0;
    int out_len = 0;
    int final_len = 0;
    hermes_status status;

    if (!recipient || !context || !ciphertext || !plaintext || !plaintext_len) {
        return HERMES_ERR_ARGUMENT;
    }
    if (ciphertext_len < HERMES_EPHEMERAL_LEN + 2u + HERMES_AEAD_TAG_LEN) {
        return HERMES_ERR_FORMAT;
    }
    pt_len = hermes_read_be16(ciphertext + HERMES_EPHEMERAL_LEN);
    if ((size_t) pt_len + HERMES_EPHEMERAL_LEN + 2u + HERMES_AEAD_TAG_LEN != ciphertext_len) {
        return HERMES_ERR_FORMAT;
    }
    if (*plaintext_len < pt_len) {
        return HERMES_ERR_RANGE;
    }

    status = hermes_x25519_shared_secret(recipient->box_private, ciphertext, shared);
    if (status != HERMES_OK) {
        return status;
    }
    if (hermes_build_aead_ad(context, ad, &ad_len) != HERMES_OK) {
        hermes_secure_bzero(shared, sizeof(shared));
        return HERMES_ERR_PROTOCOL;
    }
    if (hermes_sha256(ad, ad_len, salt) != HERMES_OK ||
        hermes_hkdf_derive(shared, sizeof(shared), salt, sizeof(salt), derived, sizeof(derived)) != HERMES_OK) {
        hermes_secure_bzero(shared, sizeof(shared));
        return HERMES_ERR_CRYPTO;
    }

    cipher = EVP_CIPHER_CTX_new();
    if (!cipher) {
        hermes_secure_bzero(shared, sizeof(shared));
        hermes_secure_bzero(derived, sizeof(derived));
        return HERMES_ERR_CRYPTO;
    }
    if (EVP_DecryptInit_ex(cipher, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_AEAD_SET_IVLEN, HERMES_AEAD_NONCE_LEN, NULL) != 1 ||
        EVP_DecryptInit_ex(cipher, NULL, NULL, derived, derived + 32u) != 1 ||
        EVP_DecryptUpdate(cipher, NULL, &out_len, ad, (int) ad_len) != 1 ||
        EVP_DecryptUpdate(cipher,
                          plaintext,
                          &out_len,
                          ciphertext + HERMES_EPHEMERAL_LEN + 2u,
                          pt_len) != 1 ||
        EVP_CIPHER_CTX_ctrl(cipher,
                            EVP_CTRL_AEAD_SET_TAG,
                            HERMES_AEAD_TAG_LEN,
                            (void *) (ciphertext + HERMES_EPHEMERAL_LEN + 2u + pt_len)) != 1 ||
        EVP_DecryptFinal_ex(cipher, plaintext + out_len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(cipher);
        hermes_secure_bzero(shared, sizeof(shared));
        hermes_secure_bzero(derived, sizeof(derived));
        return HERMES_ERR_VERIFY;
    }
    EVP_CIPHER_CTX_free(cipher);
    *plaintext_len = (size_t) pt_len;
    hermes_secure_bzero(shared, sizeof(shared));
    hermes_secure_bzero(derived, sizeof(derived));
    return HERMES_OK;
}

hermes_status hermes_pow_hash(const hermes_envelope *env, uint64_t nonce, uint8_t out[HERMES_HASH_LEN]) {
    uint8_t digest_payload[HERMES_HASH_LEN];
    uint8_t *buffer;
    size_t buffer_len = 1u + HERMES_PUBLIC_ID_LEN + HERMES_RECIPIENT_HINT_LEN + 8u + 8u + HERMES_HASH_LEN +
                        HERMES_SIGNATURE_LEN + 1u + 4u + 8u;
    size_t off = 0;
    hermes_status status;
    if (!env || !out) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_sha256(env->encrypted_payload, env->payload_size, digest_payload);
    if (status != HERMES_OK) {
        return status;
    }
    buffer = (uint8_t *) malloc(buffer_len);
    if (!buffer) {
        return HERMES_ERR_MEMORY;
    }
    buffer[off++] = env->protocol_version;
    memcpy(buffer + off, env->sender_public_identity.sig_public, HERMES_SIG_PUBLIC_LEN);
    off += HERMES_SIG_PUBLIC_LEN;
    memcpy(buffer + off, env->sender_public_identity.box_public, HERMES_BOX_PUBLIC_LEN);
    off += HERMES_BOX_PUBLIC_LEN;
    memcpy(buffer + off, env->recipient_hint, HERMES_RECIPIENT_HINT_LEN);
    off += HERMES_RECIPIENT_HINT_LEN;
    hermes_write_be64(buffer + off, env->created_at_unix);
    off += 8u;
    hermes_write_be64(buffer + off, env->expires_at_unix);
    off += 8u;
    memcpy(buffer + off, digest_payload, HERMES_HASH_LEN);
    off += HERMES_HASH_LEN;
    memcpy(buffer + off, env->signature, HERMES_SIGNATURE_LEN);
    off += HERMES_SIGNATURE_LEN;
    buffer[off++] = env->pow_algorithm;
    hermes_write_be32(buffer + off, env->pow_difficulty);
    off += 4u;
    hermes_write_be64(buffer + off, nonce);
    off += 8u;
    if (off != buffer_len) {
        free(buffer);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_sha256(buffer, buffer_len, out);
    free(buffer);
    return status;
}

int hermes_pow_meets_difficulty(const uint8_t hash[HERMES_HASH_LEN], uint32_t difficulty) {
    uint32_t bits = 0;
    size_t i;
    static const uint8_t leading_zeroes[16] = {4u, 3u, 2u, 2u, 1u, 1u, 1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    if (!hash) {
        return 0;
    }
    for (i = 0; i < HERMES_HASH_LEN; ++i) {
        if (hash[i] == 0) {
            bits += 8u;
            if (bits >= difficulty) {
                return 1;
            }
            continue;
        }
        bits += leading_zeroes[(hash[i] >> 4) & 0x0f];
        if ((hash[i] >> 4) == 0) {
            bits += leading_zeroes[hash[i] & 0x0f];
        }
        return bits >= difficulty;
    }
    return bits >= difficulty;
}

hermes_status hermes_pow_solve(hermes_envelope *env, uint64_t max_attempts) {
    uint64_t nonce;
    uint8_t hash[HERMES_HASH_LEN];
    if (!env) {
        return HERMES_ERR_ARGUMENT;
    }
    for (nonce = 0; nonce < max_attempts; ++nonce) {
        if (hermes_pow_hash(env, nonce, hash) != HERMES_OK) {
            return HERMES_ERR_CRYPTO;
        }
        if (hermes_pow_meets_difficulty(hash, env->pow_difficulty)) {
            env->pow_nonce = nonce;
            return HERMES_OK;
        }
    }
    return HERMES_ERR_QUOTA;
}

hermes_status hermes_pow_verify(const hermes_envelope *env) {
    uint8_t hash[HERMES_HASH_LEN];
    if (!env) {
        return HERMES_ERR_ARGUMENT;
    }
    if (env->pow_algorithm != HERMES_POW_ALG_SHA256_LZ) {
        return HERMES_ERR_PROTOCOL;
    }
    if (hermes_pow_hash(env, env->pow_nonce, hash) != HERMES_OK) {
        return HERMES_ERR_CRYPTO;
    }
    return hermes_pow_meets_difficulty(hash, env->pow_difficulty) ? HERMES_OK : HERMES_ERR_VERIFY;
}
