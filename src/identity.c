#include "hermes/identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hermes/crypto.h"
#include "hermes/util.h"

static hermes_status hermes_write_hex_line(FILE *fp, const char *key, const uint8_t *value, size_t value_len) {
    char hex[HERMES_HASH_LEN * 2u + 1u];
    if (value_len > HERMES_HASH_LEN) {
        return HERMES_ERR_RANGE;
    }
    if (hermes_hex_encode(value, value_len, hex, sizeof(hex)) != HERMES_OK) {
        return HERMES_ERR_RANGE;
    }
    if (fprintf(fp, "%s=%s\n", key, hex) < 0) {
        return HERMES_ERR_IO;
    }
    return HERMES_OK;
}

static void hermes_trim_newline(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1u] == '\n' || line[len - 1u] == '\r')) {
        line[--len] = '\0';
    }
}

static hermes_status hermes_parse_hex_value(const char *line,
                                            const char *key,
                                            uint8_t *dst,
                                            size_t dst_len) {
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') {
        return HERMES_ERR_FORMAT;
    }
    return hermes_hex_decode(line + key_len + 1u, dst, dst_len);
}

static hermes_status hermes_parse_string_value(const char *line,
                                               const char *key,
                                               char *dst,
                                               size_t dst_len) {
    const char *value;
    size_t key_len = strlen(key);
    size_t value_len;
    if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') {
        return HERMES_ERR_FORMAT;
    }
    value = line + key_len + 1u;
    value_len = strlen(value);
    if (value_len >= dst_len) {
        return HERMES_ERR_RANGE;
    }
    memcpy(dst, value, value_len + 1u);
    return HERMES_OK;
}

hermes_status hermes_identity_save(const char *path, const hermes_identity *identity) {
    FILE *fp;
    hermes_status status;
    if (!path || !identity) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_mkdir_p("."), 0) {
        /* no-op to silence style-only concerns */
    }
    fp = fopen(path, "wb");
    if (!fp) {
        return HERMES_ERR_IO;
    }
    if (fprintf(fp, "HERMES-IDENTITY-V1\nalias=%s\n", identity->alias) < 0) {
        fclose(fp);
        return HERMES_ERR_IO;
    }
    status = hermes_write_hex_line(fp, "sig_public", identity->public_identity.sig_public, HERMES_SIG_PUBLIC_LEN);
    if (status == HERMES_OK) {
        status = hermes_write_hex_line(fp, "sig_private", identity->sig_private, HERMES_SIG_PRIVATE_LEN);
    }
    if (status == HERMES_OK) {
        status = hermes_write_hex_line(fp, "box_public", identity->public_identity.box_public, HERMES_BOX_PUBLIC_LEN);
    }
    if (status == HERMES_OK) {
        status = hermes_write_hex_line(fp, "box_private", identity->box_private, HERMES_BOX_PRIVATE_LEN);
    }
    if (fclose(fp) != 0 && status == HERMES_OK) {
        status = HERMES_ERR_IO;
    }
    return status;
}

hermes_status hermes_identity_load(const char *path, hermes_identity *identity) {
    FILE *fp;
    char line[256];
    int saw_header = 0;
    int have_alias = 0;
    int have_sig_pub = 0;
    int have_sig_priv = 0;
    int have_box_pub = 0;
    int have_box_priv = 0;

    if (!path || !identity) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(identity, 0, sizeof(*identity));

    fp = fopen(path, "rb");
    if (!fp) {
        return HERMES_ERR_IO;
    }
    while (fgets(line, sizeof(line), fp)) {
        hermes_trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }
        if (!saw_header) {
            if (strcmp(line, "HERMES-IDENTITY-V1") != 0) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            saw_header = 1;
            continue;
        }
        if (strncmp(line, "alias=", 6) == 0) {
            if (hermes_parse_string_value(line, "alias", identity->alias, sizeof(identity->alias)) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_alias = 1;
        } else if (strncmp(line, "sig_public=", 11) == 0) {
            if (hermes_parse_hex_value(line, "sig_public", identity->public_identity.sig_public, HERMES_SIG_PUBLIC_LEN) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_sig_pub = 1;
        } else if (strncmp(line, "sig_private=", 12) == 0) {
            if (hermes_parse_hex_value(line, "sig_private", identity->sig_private, HERMES_SIG_PRIVATE_LEN) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_sig_priv = 1;
        } else if (strncmp(line, "box_public=", 11) == 0) {
            if (hermes_parse_hex_value(line, "box_public", identity->public_identity.box_public, HERMES_BOX_PUBLIC_LEN) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_box_pub = 1;
        } else if (strncmp(line, "box_private=", 12) == 0) {
            if (hermes_parse_hex_value(line, "box_private", identity->box_private, HERMES_BOX_PRIVATE_LEN) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_box_priv = 1;
        } else {
            fclose(fp);
            return HERMES_ERR_FORMAT;
        }
    }
    fclose(fp);
    if (!saw_header || !have_alias || !have_sig_pub || !have_sig_priv || !have_box_pub || !have_box_priv) {
        return HERMES_ERR_FORMAT;
    }
    return HERMES_OK;
}

hermes_status hermes_contact_save(const char *path, const hermes_contact *contact) {
    FILE *fp;
    hermes_status status;
    if (!path || !contact) {
        return HERMES_ERR_ARGUMENT;
    }
    fp = fopen(path, "wb");
    if (!fp) {
        return HERMES_ERR_IO;
    }
    if (fprintf(fp, "HERMES-CONTACT-V1\nalias=%s\n", contact->alias) < 0) {
        fclose(fp);
        return HERMES_ERR_IO;
    }
    status = hermes_write_hex_line(fp, "sig_public", contact->public_identity.sig_public, HERMES_SIG_PUBLIC_LEN);
    if (status == HERMES_OK) {
        status = hermes_write_hex_line(fp, "box_public", contact->public_identity.box_public, HERMES_BOX_PUBLIC_LEN);
    }
    if (status == HERMES_OK) {
        status = hermes_write_hex_line(fp, "recipient_hint", contact->recipient_hint, HERMES_RECIPIENT_HINT_LEN);
    }
    if (status == HERMES_OK) {
        status = hermes_write_hex_line(fp, "fingerprint", contact->fingerprint, HERMES_HASH_LEN);
    }
    if (fclose(fp) != 0 && status == HERMES_OK) {
        status = HERMES_ERR_IO;
    }
    return status;
}

hermes_status hermes_contact_load(const char *path, hermes_contact *contact) {
    FILE *fp;
    char line[256];
    int saw_header = 0;
    int have_alias = 0;
    int have_sig_pub = 0;
    int have_box_pub = 0;
    int have_hint = 0;
    int have_fingerprint = 0;

    if (!path || !contact) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(contact, 0, sizeof(*contact));

    fp = fopen(path, "rb");
    if (!fp) {
        return HERMES_ERR_IO;
    }
    while (fgets(line, sizeof(line), fp)) {
        hermes_trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }
        if (!saw_header) {
            if (strcmp(line, "HERMES-CONTACT-V1") != 0) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            saw_header = 1;
            continue;
        }
        if (strncmp(line, "alias=", 6) == 0) {
            if (hermes_parse_string_value(line, "alias", contact->alias, sizeof(contact->alias)) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_alias = 1;
        } else if (strncmp(line, "sig_public=", 11) == 0) {
            if (hermes_parse_hex_value(line, "sig_public", contact->public_identity.sig_public, HERMES_SIG_PUBLIC_LEN) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_sig_pub = 1;
        } else if (strncmp(line, "box_public=", 11) == 0) {
            if (hermes_parse_hex_value(line, "box_public", contact->public_identity.box_public, HERMES_BOX_PUBLIC_LEN) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_box_pub = 1;
        } else if (strncmp(line, "recipient_hint=", 15) == 0) {
            if (hermes_parse_hex_value(line, "recipient_hint", contact->recipient_hint, HERMES_RECIPIENT_HINT_LEN) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_hint = 1;
        } else if (strncmp(line, "fingerprint=", 12) == 0) {
            if (hermes_parse_hex_value(line, "fingerprint", contact->fingerprint, HERMES_HASH_LEN) != HERMES_OK) {
                fclose(fp);
                return HERMES_ERR_FORMAT;
            }
            have_fingerprint = 1;
        } else {
            fclose(fp);
            return HERMES_ERR_FORMAT;
        }
    }
    fclose(fp);
    if (!saw_header || !have_alias || !have_sig_pub || !have_box_pub || !have_hint || !have_fingerprint) {
        return HERMES_ERR_FORMAT;
    }
    return HERMES_OK;
}
