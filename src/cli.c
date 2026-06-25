#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hermes/bundle.h"
#include "hermes/crypto.h"
#include "hermes/envelope.h"
#include "hermes/identity.h"
#include "hermes/platform.h"
#include "hermes/relay.h"
#include "hermes/store.h"
#include "hermes/transport.h"
#include "hermes/util.h"
#include "hermes/version.h"

typedef struct hermes_cli_command_doc {
    const char *name;
    const char *summary;
    const char *usage;
    const char *details;
    const char *examples;
} hermes_cli_command_doc;

static const char *hermes_arg_value(int argc, char **argv, const char *name) {
    int i;
    for (i = 2; i < argc - 1; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int hermes_arg_present(int argc, char **argv, const char *name) {
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int hermes_is_help_flag(const char *arg) {
    return arg && (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0);
}

static int hermes_command_help_requested(int argc, char **argv) {
    int i;
    for (i = 2; i < argc; ++i) {
        if (hermes_is_help_flag(argv[i])) {
            return 1;
        }
    }
    return 0;
}

static const hermes_cli_command_doc hermes_cli_docs[] = {
    {
        "genid",
        "Generate a new local identity and show its public fingerprint.",
        "hermes-cli genid --identity PATH --alias NAME",
        "Creates a fresh Ed25519 and X25519 identity pair, stores it on disk, and prints the public recipient hint and fingerprint needed for out-of-band contact exchange. Run this once per local identity.",
        "  hermes-cli genid --identity ./alice.id --alias alice\n"
        "  hermes-cli export-contact --identity ./alice.id --out ./alice.contact"
    },
    {
        "fingerprint",
        "Show the fingerprint and recipient hint of an identity or contact.",
        "hermes-cli fingerprint --identity PATH | --contact PATH",
        "Loads either a full private identity or a public contact file and prints the stable public fingerprint plus the recipient hint used for message recognition. Use this to verify contact exchange over voice, paper, or trusted in-person channels.",
        "  hermes-cli fingerprint --identity ./alice.id\n"
        "  hermes-cli fingerprint --contact ./bob.contact"
    },
    {
        "export-contact",
        "Export the public contact material derived from a private identity.",
        "hermes-cli export-contact --identity PATH --out PATH",
        "Builds a transport-safe public contact file containing only the data needed for other users to encrypt to that identity. The private signing and decryption material never leaves the identity file.",
        "  hermes-cli export-contact --identity ./alice.id --out ./alice.contact"
    },
    {
        "import-contact",
        "Validate and copy a received public contact into local storage.",
        "hermes-cli import-contact --in PATH --out PATH",
        "Loads a public contact file, parses it defensively, and rewrites it to a clean local path. This is useful when normalizing contacts received over USB, file share, or relay export media.",
        "  hermes-cli import-contact --in /media/usb/bob.contact --out ./contacts/bob.contact"
    },
    {
        "create",
        "Create one encrypted, signed, proof-of-work protected envelope.",
        "hermes-cli create --identity PATH --contact PATH --store DIR --message TEXT [--out PATH] [--ttl-hours H] [--difficulty D]",
        "Encrypts a short plaintext to one recipient, signs the canonical envelope, computes sender-side proof of work, stores the accepted envelope locally, and optionally writes the wire-format envelope to a file. TTL above the protocol maximum is rejected by the protocol rules.",
        "  hermes-cli create --identity ./alice.id --contact ./bob.contact --store ./alice-store \\\n"
        "    --message \"Meet at well 18:00\" --out ./msg.bin --difficulty 28"
    },
    {
        "verify",
        "Parse and verify one serialized envelope.",
        "hermes-cli verify --envelope PATH",
        "Checks canonical parsing, signature validity, proof of work, TTL bounds, and envelope integrity without decrypting the payload. This is the safe first inspection step for any envelope received over an untrusted transport.",
        "  hermes-cli verify --envelope ./msg.bin"
    },
    {
        "decrypt",
        "Decrypt one envelope using the recipient identity.",
        "hermes-cli decrypt --identity PATH --envelope PATH",
        "Loads the recipient identity, verifies that the local recipient hint matches the envelope, and decrypts the short plaintext to standard output. Use this only on endpoints that are intended to receive the message.",
        "  hermes-cli decrypt --identity ./bob.id --envelope ./msg.bin"
    },
    {
        "import-bundle",
        "Import a bundle of envelopes into a local store.",
        "hermes-cli import-bundle --store DIR --in PATH",
        "Reads a bundle produced by export or relay tooling, verifies and stores each valid envelope under local policy, and reports how many were accepted. This is the standard offline intake path for USB and file handoff.",
        "  hermes-cli import-bundle --store ./relay-store --in ./handoff.bundle"
    },
    {
        "export-bundle",
        "Export the forwardable contents of a local store as one bundle.",
        "hermes-cli export-bundle --store DIR --out PATH",
        "Walks the local store, selects envelopes still eligible for forwarding, and writes them as one opaque bundle suitable for file copy, USB transfer, QR chunking gateways, or manual carriage.",
        "  hermes-cli export-bundle --store ./relay-store --out ./handoff.bundle"
    },
    {
        "serve",
        "Run a simple foreground TCP sync listener against one store.",
        "hermes-cli serve --store DIR --listen HOST:PORT",
        "Starts the minimal transport listener without the integrated relay package. This mode is useful for controlled lab tests and simple direct store-to-store synchronization, but the relay node commands are usually the better operational choice.",
        "  hermes-cli serve --store ./relay-store --listen 0.0.0.0:9440"
    },
    {
        "sync",
        "Synchronize one local store with one peer over TCP.",
        "hermes-cli sync --store DIR --peer HOST:PORT",
        "Connects to a peer, exchanges inventories, requests missing envelopes, imports validated traffic, and sends any locally missing traffic back to the peer. Plaintext never traverses the transport.",
        "  hermes-cli sync --store ./relay-store --peer 192.168.1.8:9440"
    },
    {
        "stats",
        "Show the current local store inventory and pressure indicators.",
        "hermes-cli stats --store DIR",
        "Prints envelope count, byte usage, expired count, and proof-of-work range for the store. Use this to assess store pressure before changing quotas or relay policy.",
        "  hermes-cli stats --store ./relay-store"
    },
    {
        "cleanup",
        "Expire old traffic and enforce local store hygiene immediately.",
        "hermes-cli cleanup --store DIR",
        "Runs the same cleanup path used by the relay runtime: expire forward-invalid traffic, maintain dedup markers, and enforce local quota policy. This is useful after bulk imports or before snapshot export.",
        "  hermes-cli cleanup --store ./relay-store"
    },
    {
        "relay-init",
        "Create a complete integrated relay root and initial configuration.",
        "hermes-cli relay-init --root DIR [--listen HOST:PORT] [--sync-interval S] [--import-interval S] [--export-interval S] [--cleanup-interval S] [--heartbeat-interval S] [--log-path PATH] [--log-rotate-bytes N] [--log-rotate-keep N] [--log-stderr]",
        "Initializes the store, import, archive, export, logs, and run directories together with the relay configuration file. This is the first command to run when provisioning a relay on a laptop, Raspberry Pi, or bridge node.",
        "  hermes-cli relay-init --root ./relay-a --listen 0.0.0.0:9440 --heartbeat-interval 15"
    },
    {
        "relay-add-peer",
        "Add or update one known peer in the relay addressbook.",
        "hermes-cli relay-add-peer --root DIR --peer HOST:PORT [--alias NAME]",
        "Stores a peer endpoint in the relay addressbook used for periodic synchronization. Manual peers are retained even if they later go offline, which matches crisis conditions where nodes may disappear and return.",
        "  hermes-cli relay-add-peer --root ./relay-a --peer 192.168.1.44:9440 --alias neighbor"
    },
    {
        "relay-peers",
        "List the relay addressbook together with peer health state.",
        "hermes-cli relay-peers --root DIR",
        "Shows manual and auto-learned peers, failure counters, last-attempt timestamps, and whether a peer is active or inactive. This is the primary local view for relay-to-relay topology health.",
        "  hermes-cli relay-peers --root ./relay-a"
    },
    {
        "relay-drop",
        "Queue a local file into the relay import directory.",
        "hermes-cli relay-drop --root DIR --in PATH",
        "Copies a bundle or envelope file into the relay import queue with a unique local filename so that the relay runtime or the manual import command can process it safely later. Use this for operator-mediated handoff.",
        "  hermes-cli relay-drop --root ./relay-a --in ./handoff.bundle"
    },
    {
        "relay-imports",
        "Process queued files from the relay import directory immediately.",
        "hermes-cli relay-imports --root DIR",
        "Scans the relay import directory, validates regular files, imports accepted envelopes into the relay store, and archives processed files. This is the manual offline-first path when the long-running relay service is not active.",
        "  hermes-cli relay-imports --root ./relay-a"
    },
    {
        "relay-export-latest",
        "Refresh and copy the relay's latest outbound bundle snapshot.",
        "hermes-cli relay-export-latest --root DIR --out PATH",
        "Builds the relay's current forwardable bundle snapshot and copies it to a caller-specified path. This is the preferred handoff command for USB export or local courier workflows.",
        "  hermes-cli relay-export-latest --root ./relay-a --out ./handoff.bundle"
    },
    {
        "relay-run",
        "Run the integrated relay service in the foreground.",
        "hermes-cli relay-run --root DIR [--listen HOST:PORT] [--log-path PATH] [--log-rotate-bytes N] [--log-rotate-keep N] [--heartbeat-interval S] [--log-stderr]",
        "Starts the full relay node: inbound TCP sync, import queue processing, peer synchronization, latest bundle export, structured logs, heartbeat status, and PID protection. This is the operational service command for LAN relays and Internet bridge relays alike.",
        "  hermes-cli relay-run --root ./relay-a --log-stderr\n"
        "  hermes-cli relay-status --root ./relay-a"
    },
    {
        "relay-status",
        "Print the current relay heartbeat status document.",
        "hermes-cli relay-status --root DIR",
        "Reads the relay heartbeat file and prints the local JSON status exactly as written by the service runtime. This is the fastest way to confirm liveness, listen address, peer counts, and recent sync activity.",
        "  hermes-cli relay-status --root ./relay-a"
    }
};

static const hermes_cli_command_doc *hermes_cli_find_doc(const char *name) {
    size_t i;
    for (i = 0; i < (sizeof(hermes_cli_docs) / sizeof(hermes_cli_docs[0])); ++i) {
        if (strcmp(hermes_cli_docs[i].name, name) == 0) {
            return &hermes_cli_docs[i];
        }
    }
    return NULL;
}

static void hermes_print_command_help(const hermes_cli_command_doc *doc) {
    if (!doc) {
        return;
    }
    printf("Hermes Relay CLI\n");
    printf("Command: %s\n\n", doc->name);
    printf("%s\n\n", doc->summary);
    printf("Usage:\n  %s\n\n", doc->usage);
    printf("Description:\n  %s\n", doc->details);
    if (doc->examples && doc->examples[0] != '\0') {
        printf("\nExamples:\n%s\n", doc->examples);
    }
}

static void hermes_print_global_help(void) {
    puts("Hermes Relay CLI");
    puts("Offline-first encrypted messaging and relay operations toolkit");
    printf("Protocol V%u  |  max envelope %u bytes  |  default TTL %u days\n\n",
           HERMES_PROTOCOL_VERSION,
           HERMES_MAX_ENVELOPE_SIZE,
           HERMES_DEFAULT_TTL_SECONDS / 86400u);
    puts("Hermes Relay is designed for one-to-one short messages that can survive");
    puts("degraded networks through store-carry-forward relay, file transfer, and LAN sync.\n");
    puts("Command groups:");
    puts("  Identity and contacts");
    puts("    genid, fingerprint, export-contact, import-contact");
    puts("  Message handling");
    puts("    create, verify, decrypt");
    puts("  Store and transfer");
    puts("    import-bundle, export-bundle, serve, sync, stats, cleanup");
    puts("  Integrated relay node");
    puts("    relay-init, relay-add-peer, relay-peers, relay-drop, relay-imports,");
    puts("    relay-export-latest, relay-run, relay-status\n");
    puts("Common operator paths:");
    puts("  1. Identity setup");
    puts("     hermes-cli genid --identity ./alice.id --alias alice");
    puts("     hermes-cli export-contact --identity ./alice.id --out ./alice.contact");
    puts("  2. Create one message");
    puts("     hermes-cli create --identity ./alice.id --contact ./bob.contact --store ./alice-store --message \"Meet at well 18:00\"");
    puts("  3. Provision one relay");
    puts("     hermes-cli relay-init --root ./relay-a --listen 0.0.0.0:9440");
    puts("     hermes-cli relay-add-peer --root ./relay-a --peer 192.168.1.44:9440 --alias neighbor");
    puts("     hermes-cli relay-run --root ./relay-a\n");
    puts("Detailed help:");
    puts("  hermes-cli help <command>");
    puts("  hermes-cli <command> --help\n");
    puts("Documentation:");
    puts("  README.md");
    puts("  docs/cli-guide.md");
    puts("  docs/relay-node.md");
    puts("  docs/service-operations.md");
}

static int hermes_print_help_for_name(const char *name) {
    const hermes_cli_command_doc *doc = hermes_cli_find_doc(name);
    if (!doc) {
        fprintf(stderr, "unknown command: %s\n", name ? name : "(null)");
        fprintf(stderr, "run `hermes-cli --help` to list commands\n");
        return 1;
    }
    hermes_print_command_help(doc);
    return 0;
}

static void hermes_print_hex_line(const char *label, const uint8_t *data, size_t len) {
    char hex[(HERMES_HASH_LEN * 2u) + 1u];
    if (len > HERMES_HASH_LEN) {
        return;
    }
    if (hermes_hex_encode(data, len, hex, sizeof(hex)) == HERMES_OK) {
        printf("%s: %s\n", label, hex);
    }
}

static int hermes_open_store_or_die(hermes_store *store, const char *path) {
    hermes_status status;
    hermes_store_policy policy;
    hermes_store_default_policy(&policy);
    status = hermes_store_open(store, path, &policy);
    if (status != HERMES_OK) {
        fprintf(stderr, "store error: %s\n", hermes_status_string(status));
        return 0;
    }
    return 1;
}

static int cmd_genid(int argc, char **argv) {
    const char *identity_path = hermes_arg_value(argc, argv, "--identity");
    const char *alias = hermes_arg_value(argc, argv, "--alias");
    hermes_identity identity;
    hermes_contact contact;
    hermes_status status;
    if (!identity_path || !alias) {
        fprintf(stderr, "usage: hermes-cli genid --identity PATH --alias NAME\n");
        return 1;
    }
    status = hermes_identity_generate(alias, &identity);
    if (status == HERMES_OK) {
        status = hermes_identity_save(identity_path, &identity);
    }
    if (status == HERMES_OK) {
        status = hermes_contact_from_identity(&identity, &contact);
    }
    if (status != HERMES_OK) {
        fprintf(stderr, "genid failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("identity written: %s\n", identity_path);
    hermes_print_hex_line("recipient_hint", contact.recipient_hint, HERMES_RECIPIENT_HINT_LEN);
    hermes_print_hex_line("fingerprint", contact.fingerprint, HERMES_HASH_LEN);
    return 0;
}

static int cmd_fingerprint(int argc, char **argv) {
    const char *identity_path = hermes_arg_value(argc, argv, "--identity");
    const char *contact_path = hermes_arg_value(argc, argv, "--contact");
    hermes_contact contact;
    hermes_identity identity;
    hermes_status status;
    if (!identity_path && !contact_path) {
        fprintf(stderr, "usage: hermes-cli fingerprint --identity PATH | --contact PATH\n");
        return 1;
    }
    if (identity_path) {
        status = hermes_identity_load(identity_path, &identity);
        if (status == HERMES_OK) {
            status = hermes_contact_from_identity(&identity, &contact);
        }
    } else {
        status = hermes_contact_load(contact_path, &contact);
    }
    if (status != HERMES_OK) {
        fprintf(stderr, "fingerprint failed: %s\n", hermes_status_string(status));
        return 1;
    }
    hermes_print_hex_line("fingerprint", contact.fingerprint, HERMES_HASH_LEN);
    hermes_print_hex_line("recipient_hint", contact.recipient_hint, HERMES_RECIPIENT_HINT_LEN);
    return 0;
}

static int cmd_export_contact(int argc, char **argv) {
    const char *identity_path = hermes_arg_value(argc, argv, "--identity");
    const char *out_path = hermes_arg_value(argc, argv, "--out");
    hermes_identity identity;
    hermes_contact contact;
    hermes_status status;
    if (!identity_path || !out_path) {
        fprintf(stderr, "usage: hermes-cli export-contact --identity PATH --out PATH\n");
        return 1;
    }
    status = hermes_identity_load(identity_path, &identity);
    if (status == HERMES_OK) {
        status = hermes_contact_from_identity(&identity, &contact);
    }
    if (status == HERMES_OK) {
        status = hermes_contact_save(out_path, &contact);
    }
    if (status != HERMES_OK) {
        fprintf(stderr, "export-contact failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("contact written: %s\n", out_path);
    return 0;
}

static int cmd_import_contact(int argc, char **argv) {
    const char *in_path = hermes_arg_value(argc, argv, "--in");
    const char *out_path = hermes_arg_value(argc, argv, "--out");
    hermes_contact contact;
    hermes_status status;
    if (!in_path || !out_path) {
        fprintf(stderr, "usage: hermes-cli import-contact --in PATH --out PATH\n");
        return 1;
    }
    status = hermes_contact_load(in_path, &contact);
    if (status == HERMES_OK) {
        status = hermes_contact_save(out_path, &contact);
    }
    if (status != HERMES_OK) {
        fprintf(stderr, "import-contact failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("contact imported: %s\n", out_path);
    return 0;
}

static int cmd_create(int argc, char **argv) {
    const char *identity_path = hermes_arg_value(argc, argv, "--identity");
    const char *contact_path = hermes_arg_value(argc, argv, "--contact");
    const char *store_path = hermes_arg_value(argc, argv, "--store");
    const char *message = hermes_arg_value(argc, argv, "--message");
    const char *out_path = hermes_arg_value(argc, argv, "--out");
    const char *ttl_hours_text = hermes_arg_value(argc, argv, "--ttl-hours");
    const char *difficulty_text = hermes_arg_value(argc, argv, "--difficulty");
    hermes_identity sender;
    hermes_contact recipient;
    hermes_store store;
    hermes_envelope env;
    uint64_t now_unix = (uint64_t) hermes_now_utc();
    uint64_t ttl_seconds = HERMES_DEFAULT_TTL_SECONDS;
    uint32_t pow_difficulty = 28u;
    hermes_status status;
    uint8_t wire[HERMES_MAX_ENVELOPE_SIZE];
    size_t wire_len = sizeof(wire);

    if (!identity_path || !contact_path || !store_path || !message) {
        fprintf(stderr, "usage: hermes-cli create --identity PATH --contact PATH --store DIR --message TEXT [--out PATH] [--ttl-hours H] [--difficulty D]\n");
        return 1;
    }
    if (ttl_hours_text) {
        ttl_seconds = (uint64_t) strtoull(ttl_hours_text, NULL, 10) * 3600u;
    }
    if (difficulty_text) {
        pow_difficulty = (uint32_t) strtoul(difficulty_text, NULL, 10);
    }
    if (!hermes_open_store_or_die(&store, store_path)) {
        return 1;
    }
    status = hermes_identity_load(identity_path, &sender);
    if (status == HERMES_OK) {
        status = hermes_contact_load(contact_path, &recipient);
    }
    if (status == HERMES_OK) {
        status = hermes_envelope_create(&env,
                                        &sender,
                                        &recipient,
                                        (const uint8_t *) message,
                                        strlen(message),
                                        now_unix,
                                        ttl_seconds,
                                        0u,
                                        pow_difficulty);
    }
    if (status == HERMES_OK) {
        status = hermes_store_add_envelope(&store, &env, now_unix);
    }
    if (status == HERMES_OK && out_path) {
        status = hermes_envelope_encode(&env, wire, &wire_len);
        if (status == HERMES_OK) {
            status = hermes_write_file_atomic(out_path, wire, wire_len);
        }
    }
    if (status != HERMES_OK) {
        fprintf(stderr, "create failed: %s\n", hermes_status_string(status));
        return 1;
    }
    hermes_print_hex_line("envelope_id", env.envelope_id, HERMES_ID_LEN);
    printf("pow_difficulty: %u\n", env.pow_difficulty);
    printf("stored_at: %s\n", store_path);
    if (out_path) {
        printf("envelope_file: %s\n", out_path);
    }
    return 0;
}

static int cmd_verify(int argc, char **argv) {
    const char *path = hermes_arg_value(argc, argv, "--envelope");
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_envelope env;
    hermes_status status;
    if (!path) {
        fprintf(stderr, "usage: hermes-cli verify --envelope PATH\n");
        return 1;
    }
    status = hermes_read_file(path, &raw, &raw_len);
    if (status == HERMES_OK) {
        status = hermes_envelope_decode(raw, raw_len, &env);
    }
    if (status == HERMES_OK) {
        status = hermes_envelope_verify(&env, (uint64_t) hermes_now_utc());
    }
    free(raw);
    if (status != HERMES_OK) {
        fprintf(stderr, "verify failed: %s\n", hermes_status_string(status));
        return 1;
    }
    hermes_print_hex_line("envelope_id", env.envelope_id, HERMES_ID_LEN);
    printf("payload_size: %u\n", env.payload_size);
    printf("pow_difficulty: %u\n", env.pow_difficulty);
    return 0;
}

static int cmd_decrypt(int argc, char **argv) {
    const char *identity_path = hermes_arg_value(argc, argv, "--identity");
    const char *envelope_path = hermes_arg_value(argc, argv, "--envelope");
    hermes_identity identity;
    hermes_contact self_contact;
    hermes_envelope env;
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    uint8_t plaintext[HERMES_MAX_PLAINTEXT + 1u];
    size_t plaintext_len = HERMES_MAX_PLAINTEXT;
    hermes_status status;
    if (!identity_path || !envelope_path) {
        fprintf(stderr, "usage: hermes-cli decrypt --identity PATH --envelope PATH\n");
        return 1;
    }
    status = hermes_identity_load(identity_path, &identity);
    if (status == HERMES_OK) {
        status = hermes_contact_from_identity(&identity, &self_contact);
    }
    if (status == HERMES_OK) {
        status = hermes_read_file(envelope_path, &raw, &raw_len);
    }
    if (status == HERMES_OK) {
        status = hermes_envelope_decode(raw, raw_len, &env);
    }
    if (status == HERMES_OK && !hermes_ct_equal(self_contact.recipient_hint, env.recipient_hint, HERMES_RECIPIENT_HINT_LEN)) {
        status = HERMES_ERR_VERIFY;
    }
    if (status == HERMES_OK) {
        status = hermes_decrypt_payload(&identity, &env, env.encrypted_payload, env.payload_size, plaintext, &plaintext_len);
    }
    free(raw);
    if (status != HERMES_OK) {
        fprintf(stderr, "decrypt failed: %s\n", hermes_status_string(status));
        return 1;
    }
    plaintext[plaintext_len] = '\0';
    printf("%s\n", plaintext);
    return 0;
}

static int cmd_import_bundle(int argc, char **argv) {
    const char *store_path = hermes_arg_value(argc, argv, "--store");
    const char *bundle_path = hermes_arg_value(argc, argv, "--in");
    hermes_store store;
    size_t imported = 0;
    hermes_status status;
    if (!store_path || !bundle_path) {
        fprintf(stderr, "usage: hermes-cli import-bundle --store DIR --in PATH\n");
        return 1;
    }
    if (!hermes_open_store_or_die(&store, store_path)) {
        return 1;
    }
    status = hermes_bundle_import_store(&store, bundle_path, (uint64_t) hermes_now_utc(), &imported);
    if (status != HERMES_OK) {
        fprintf(stderr, "import-bundle failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("imported: %zu\n", imported);
    return 0;
}

static int cmd_export_bundle(int argc, char **argv) {
    const char *store_path = hermes_arg_value(argc, argv, "--store");
    const char *bundle_path = hermes_arg_value(argc, argv, "--out");
    hermes_store store;
    hermes_status status;
    if (!store_path || !bundle_path) {
        fprintf(stderr, "usage: hermes-cli export-bundle --store DIR --out PATH\n");
        return 1;
    }
    if (!hermes_open_store_or_die(&store, store_path)) {
        return 1;
    }
    status = hermes_bundle_export_store(&store, bundle_path, (uint64_t) hermes_now_utc());
    if (status != HERMES_OK) {
        fprintf(stderr, "export-bundle failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("bundle written: %s\n", bundle_path);
    return 0;
}

static int cmd_serve(int argc, char **argv) {
    const char *store_path = hermes_arg_value(argc, argv, "--store");
    const char *listen_addr = hermes_arg_value(argc, argv, "--listen");
    hermes_store store;
    hermes_status status;
    if (!store_path || !listen_addr) {
        fprintf(stderr, "usage: hermes-cli serve --store DIR --listen HOST:PORT\n");
        return 1;
    }
    if (!hermes_open_store_or_die(&store, store_path)) {
        return 1;
    }
    status = hermes_transport_tcp()->serve(&store, listen_addr);
    if (status != HERMES_OK) {
        fprintf(stderr, "serve failed: %s\n", hermes_status_string(status));
        return 1;
    }
    return 0;
}

static int cmd_sync(int argc, char **argv) {
    const char *store_path = hermes_arg_value(argc, argv, "--store");
    const char *peer_addr = hermes_arg_value(argc, argv, "--peer");
    hermes_store store;
    hermes_status status;
    if (!store_path || !peer_addr) {
        fprintf(stderr, "usage: hermes-cli sync --store DIR --peer HOST:PORT\n");
        return 1;
    }
    if (!hermes_open_store_or_die(&store, store_path)) {
        return 1;
    }
    status = hermes_transport_tcp()->sync_peer(&store, peer_addr, (uint64_t) hermes_now_utc());
    if (status != HERMES_OK) {
        fprintf(stderr, "sync failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("sync complete\n");
    return 0;
}

static int cmd_stats(int argc, char **argv) {
    const char *store_path = hermes_arg_value(argc, argv, "--store");
    hermes_store store;
    hermes_store_stats stats;
    hermes_status status;
    if (!store_path) {
        fprintf(stderr, "usage: hermes-cli stats --store DIR\n");
        return 1;
    }
    if (!hermes_open_store_or_die(&store, store_path)) {
        return 1;
    }
    status = hermes_store_get_stats(&store, &stats, (uint64_t) hermes_now_utc());
    if (status != HERMES_OK) {
        fprintf(stderr, "stats failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("envelopes: %llu\n", (unsigned long long) stats.envelope_count);
    printf("bytes: %llu\n", (unsigned long long) stats.total_bytes);
    printf("expired: %llu\n", (unsigned long long) stats.expired_count);
    printf("weakest_pow: %llu\n", (unsigned long long) stats.weakest_pow);
    printf("strongest_pow: %llu\n", (unsigned long long) stats.strongest_pow);
    return 0;
}

static int cmd_cleanup(int argc, char **argv) {
    const char *store_path = hermes_arg_value(argc, argv, "--store");
    hermes_store store;
    hermes_status status;
    if (!store_path) {
        fprintf(stderr, "usage: hermes-cli cleanup --store DIR\n");
        return 1;
    }
    if (!hermes_open_store_or_die(&store, store_path)) {
        return 1;
    }
    status = hermes_store_cleanup(&store, (uint64_t) hermes_now_utc());
    if (status != HERMES_OK) {
        fprintf(stderr, "cleanup failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("cleanup complete\n");
    return 0;
}

static int cmd_relay_init(int argc, char **argv) {
    const char *root = hermes_arg_value(argc, argv, "--root");
    const char *listen = hermes_arg_value(argc, argv, "--listen");
    const char *sync_interval = hermes_arg_value(argc, argv, "--sync-interval");
    const char *import_interval = hermes_arg_value(argc, argv, "--import-interval");
    const char *export_interval = hermes_arg_value(argc, argv, "--export-interval");
    const char *cleanup_interval = hermes_arg_value(argc, argv, "--cleanup-interval");
    const char *heartbeat_interval = hermes_arg_value(argc, argv, "--heartbeat-interval");
    const char *log_path = hermes_arg_value(argc, argv, "--log-path");
    const char *log_rotate_bytes = hermes_arg_value(argc, argv, "--log-rotate-bytes");
    const char *log_rotate_keep = hermes_arg_value(argc, argv, "--log-rotate-keep");
    hermes_relay_config config;
    hermes_status status;
    if (!root) {
        fprintf(stderr, "usage: hermes-cli relay-init --root DIR [--listen HOST:PORT] [--sync-interval S] [--import-interval S] [--export-interval S] [--cleanup-interval S] [--heartbeat-interval S] [--log-path PATH] [--log-rotate-bytes N] [--log-rotate-keep N] [--log-stderr]\n");
        return 1;
    }
    hermes_relay_default_config(&config);
    if (strlen(root) >= sizeof(config.root)) {
        fprintf(stderr, "relay-init failed: root path too long\n");
        return 1;
    }
    memcpy(config.root, root, strlen(root) + 1u);
    if (listen) {
        if (strlen(listen) >= sizeof(config.listen_addr)) {
            fprintf(stderr, "relay-init failed: listen address too long\n");
            return 1;
        }
        memcpy(config.listen_addr, listen, strlen(listen) + 1u);
    }
    if (log_path) {
        if (strlen(log_path) >= sizeof(config.log_path)) {
            fprintf(stderr, "relay-init failed: log path too long\n");
            return 1;
        }
        memcpy(config.log_path, log_path, strlen(log_path) + 1u);
    }
    if (sync_interval) config.sync_interval_seconds = (uint32_t) strtoul(sync_interval, NULL, 10);
    if (import_interval) config.import_interval_seconds = (uint32_t) strtoul(import_interval, NULL, 10);
    if (export_interval) config.export_interval_seconds = (uint32_t) strtoul(export_interval, NULL, 10);
    if (cleanup_interval) config.cleanup_interval_seconds = (uint32_t) strtoul(cleanup_interval, NULL, 10);
    if (heartbeat_interval) config.heartbeat_interval_seconds = (uint32_t) strtoul(heartbeat_interval, NULL, 10);
    if (log_rotate_bytes) config.log_rotate_bytes = (uint64_t) strtoull(log_rotate_bytes, NULL, 10);
    if (log_rotate_keep) config.log_rotate_keep = (uint32_t) strtoul(log_rotate_keep, NULL, 10);
    if (hermes_arg_present(argc, argv, "--log-stderr")) config.log_mirror_stderr = 1u;
    status = hermes_relay_init_layout(&config);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-init failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("relay initialized: %s\n", root);
    printf("listen_addr: %s\n", config.listen_addr);
    printf("log_path: %s\n", config.log_path[0] != '\0' ? config.log_path : "(default)");
    return 0;
}

static int cmd_relay_add_peer(int argc, char **argv) {
    const char *root = hermes_arg_value(argc, argv, "--root");
    const char *peer = hermes_arg_value(argc, argv, "--peer");
    const char *alias = hermes_arg_value(argc, argv, "--alias");
    hermes_status status;
    if (!root || !peer) {
        fprintf(stderr, "usage: hermes-cli relay-add-peer --root DIR --peer HOST:PORT [--alias NAME]\n");
        return 1;
    }
    status = hermes_relay_add_peer(root, peer, alias);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-add-peer failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("peer added: %s\n", peer);
    return 0;
}

static int cmd_relay_peers(int argc, char **argv) {
    const char *root = hermes_arg_value(argc, argv, "--root");
    hermes_relay_peer peers[256];
    size_t count = 0;
    size_t i;
    hermes_status status;
    if (!root) {
        fprintf(stderr, "usage: hermes-cli relay-peers --root DIR\n");
        return 1;
    }
    status = hermes_relay_list_peers(root, peers, 256u, &count);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-peers failed: %s\n", hermes_status_string(status));
        return 1;
    }
    for (i = 0; i < count; ++i) {
        printf("%s", peers[i].address);
        if (peers[i].alias[0] != '\0') {
            printf(" alias=%s", peers[i].alias);
        }
        printf(" mode=%s", peers[i].learned_automatically ? "learned" : "manual");
        printf(" state=%s", peers[i].inactive ? "inactive" : "active");
        printf(" failures=%u", peers[i].consecutive_failures);
        printf(" last_success=%llu", (unsigned long long) peers[i].last_success_unix);
        printf(" last_attempt=%llu\n", (unsigned long long) peers[i].last_attempt_unix);
    }
    return 0;
}

static int cmd_relay_drop(int argc, char **argv) {
    const char *root = hermes_arg_value(argc, argv, "--root");
    const char *in_path = hermes_arg_value(argc, argv, "--in");
    uint8_t *data = NULL;
    size_t data_len = 0;
    char import_dir[1024];
    char base[256];
    char out_path[1024];
    hermes_status status;
    if (!root || !in_path) {
        fprintf(stderr, "usage: hermes-cli relay-drop --root DIR --in PATH\n");
        return 1;
    }
    status = hermes_read_file(in_path, &data, &data_len);
    if (status == HERMES_OK) status = hermes_join_path(import_dir, sizeof(import_dir), root, "import");
    if (status == HERMES_OK) status = hermes_basename_noext(in_path, base, sizeof(base));
    if (status == HERMES_OK) {
        char filename[320];
        int written = snprintf(filename,
                               sizeof(filename),
                               "%s-%llu-%lu.in",
                               base,
                               (unsigned long long) hermes_now_utc(),
                               hermes_platform_process_id());
        if (written < 0 || (size_t) written >= sizeof(filename)) {
            status = HERMES_ERR_RANGE;
        } else {
            status = hermes_join_path(out_path, sizeof(out_path), import_dir, filename);
        }
    }
    if (status == HERMES_OK) status = hermes_write_file_atomic(out_path, data, data_len);
    free(data);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-drop failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("queued import: %s\n", out_path);
    return 0;
}

static int cmd_relay_imports(int argc, char **argv) {
    const char *root = hermes_arg_value(argc, argv, "--root");
    hermes_relay_config config;
    hermes_relay_node node;
    size_t processed = 0;
    hermes_status status;
    if (!root) {
        fprintf(stderr, "usage: hermes-cli relay-imports --root DIR\n");
        return 1;
    }
    status = hermes_relay_load_config(root, &config);
    if (status == HERMES_OK) status = hermes_relay_open(&node, &config);
    if (status == HERMES_OK) status = hermes_relay_process_imports(&node, &processed);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-imports failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("processed imports: %zu\n", processed);
    return 0;
}

static int cmd_relay_export_latest(int argc, char **argv) {
    const char *root = hermes_arg_value(argc, argv, "--root");
    const char *out = hermes_arg_value(argc, argv, "--out");
    hermes_relay_config config;
    hermes_relay_node node;
    char latest_path[1024];
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_status status;
    if (!root || !out) {
        fprintf(stderr, "usage: hermes-cli relay-export-latest --root DIR --out PATH\n");
        return 1;
    }
    status = hermes_relay_load_config(root, &config);
    if (status == HERMES_OK) status = hermes_relay_open(&node, &config);
    if (status == HERMES_OK) status = hermes_relay_export_latest(&node);
    if (status == HERMES_OK) status = hermes_join_path(latest_path, sizeof(latest_path), node.export_dir, "latest.bundle");
    if (status == HERMES_OK) status = hermes_read_file(latest_path, &raw, &raw_len);
    if (status == HERMES_OK) status = hermes_write_file_atomic(out, raw, raw_len);
    free(raw);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-export-latest failed: %s\n", hermes_status_string(status));
        return 1;
    }
    printf("exported bundle: %s\n", out);
    return 0;
}

static int cmd_relay_run(int argc, char **argv) {
    const char *root = hermes_arg_value(argc, argv, "--root");
    const char *listen = hermes_arg_value(argc, argv, "--listen");
    const char *log_path = hermes_arg_value(argc, argv, "--log-path");
    const char *log_rotate_bytes = hermes_arg_value(argc, argv, "--log-rotate-bytes");
    const char *log_rotate_keep = hermes_arg_value(argc, argv, "--log-rotate-keep");
    const char *heartbeat_interval = hermes_arg_value(argc, argv, "--heartbeat-interval");
    hermes_relay_config config;
    hermes_relay_node node;
    hermes_status status;
    if (!root) {
        fprintf(stderr, "usage: hermes-cli relay-run --root DIR [--listen HOST:PORT] [--log-path PATH] [--log-rotate-bytes N] [--log-rotate-keep N] [--heartbeat-interval S] [--log-stderr]\n");
        return 1;
    }
    status = hermes_relay_load_config(root, &config);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-run failed: %s\n", hermes_status_string(status));
        return 1;
    }
    if (listen) {
        if (strlen(listen) >= sizeof(config.listen_addr)) {
            fprintf(stderr, "relay-run failed: listen address too long\n");
            return 1;
        }
        memcpy(config.listen_addr, listen, strlen(listen) + 1u);
    }
    if (log_path) {
        if (strlen(log_path) >= sizeof(config.log_path)) {
            fprintf(stderr, "relay-run failed: log path too long\n");
            return 1;
        }
        memcpy(config.log_path, log_path, strlen(log_path) + 1u);
    }
    if (log_rotate_bytes) config.log_rotate_bytes = (uint64_t) strtoull(log_rotate_bytes, NULL, 10);
    if (log_rotate_keep) config.log_rotate_keep = (uint32_t) strtoul(log_rotate_keep, NULL, 10);
    if (heartbeat_interval) config.heartbeat_interval_seconds = (uint32_t) strtoul(heartbeat_interval, NULL, 10);
    if (hermes_arg_present(argc, argv, "--log-stderr")) config.log_mirror_stderr = 1u;
    status = hermes_relay_open(&node, &config);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-run failed: %s\n", hermes_status_string(status));
        return 1;
    }
    puts("Hermes Relay integrated relay");
    printf("  root: %s\n", node.config.root);
    printf("  listen: %s\n", node.config.listen_addr);
    printf("  peers: %s\n", node.peers_path);
    printf("  import: %s\n", node.import_dir);
    printf("  export: %s/latest.bundle\n", node.export_dir);
    printf("  logs: %s\n", node.config.log_path);
    printf("  status: %s\n", node.status_path);
    puts("  mode: foreground service");
    status = hermes_relay_run(&node);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-run failed: %s\n", hermes_status_string(status));
        return 1;
    }
    return 0;
}

static int cmd_relay_status(int argc, char **argv) {
    const char *root = hermes_arg_value(argc, argv, "--root");
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    hermes_status status;
    if (!root) {
        fprintf(stderr, "usage: hermes-cli relay-status --root DIR\n");
        return 1;
    }
    status = hermes_relay_read_status(root, &raw, &raw_len);
    if (status != HERMES_OK) {
        fprintf(stderr, "relay-status failed: %s\n", hermes_status_string(status));
        return 1;
    }
    if (raw_len > 0u) {
        fwrite(raw, 1u, raw_len, stdout);
    }
    free(raw);
    return 0;
}

int main(int argc, char **argv) {
    int exit_code = 1;
    if (hermes_platform_init() != HERMES_OK) {
        fprintf(stderr, "failed to initialize platform runtime\n");
        return 1;
    }
    if (hermes_crypto_init() != HERMES_OK) {
        fprintf(stderr, "failed to initialize crypto\n");
        goto cleanup;
    }
    if (argc < 2) {
        hermes_print_global_help();
        exit_code = 0;
        goto cleanup;
    }
    if (hermes_is_help_flag(argv[1])) {
        hermes_print_global_help();
        exit_code = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "help") == 0) {
        if (argc >= 3) {
            exit_code = hermes_print_help_for_name(argv[2]);
        } else {
            hermes_print_global_help();
            exit_code = 0;
        }
        goto cleanup;
    }
    if (hermes_command_help_requested(argc, argv)) {
        exit_code = hermes_print_help_for_name(argv[1]);
        goto cleanup;
    }
    if (strcmp(argv[1], "genid") == 0) exit_code = cmd_genid(argc, argv);
    else if (strcmp(argv[1], "fingerprint") == 0) exit_code = cmd_fingerprint(argc, argv);
    else if (strcmp(argv[1], "export-contact") == 0) exit_code = cmd_export_contact(argc, argv);
    else if (strcmp(argv[1], "import-contact") == 0) exit_code = cmd_import_contact(argc, argv);
    else if (strcmp(argv[1], "create") == 0) exit_code = cmd_create(argc, argv);
    else if (strcmp(argv[1], "verify") == 0) exit_code = cmd_verify(argc, argv);
    else if (strcmp(argv[1], "decrypt") == 0) exit_code = cmd_decrypt(argc, argv);
    else if (strcmp(argv[1], "import-bundle") == 0) exit_code = cmd_import_bundle(argc, argv);
    else if (strcmp(argv[1], "export-bundle") == 0) exit_code = cmd_export_bundle(argc, argv);
    else if (strcmp(argv[1], "serve") == 0) exit_code = cmd_serve(argc, argv);
    else if (strcmp(argv[1], "sync") == 0) exit_code = cmd_sync(argc, argv);
    else if (strcmp(argv[1], "stats") == 0) exit_code = cmd_stats(argc, argv);
    else if (strcmp(argv[1], "cleanup") == 0) exit_code = cmd_cleanup(argc, argv);
    else if (strcmp(argv[1], "relay-init") == 0) exit_code = cmd_relay_init(argc, argv);
    else if (strcmp(argv[1], "relay-add-peer") == 0) exit_code = cmd_relay_add_peer(argc, argv);
    else if (strcmp(argv[1], "relay-peers") == 0) exit_code = cmd_relay_peers(argc, argv);
    else if (strcmp(argv[1], "relay-drop") == 0) exit_code = cmd_relay_drop(argc, argv);
    else if (strcmp(argv[1], "relay-imports") == 0) exit_code = cmd_relay_imports(argc, argv);
    else if (strcmp(argv[1], "relay-export-latest") == 0) exit_code = cmd_relay_export_latest(argc, argv);
    else if (strcmp(argv[1], "relay-run") == 0) exit_code = cmd_relay_run(argc, argv);
    else if (strcmp(argv[1], "relay-status") == 0) exit_code = cmd_relay_status(argc, argv);
    else {
        hermes_print_global_help();
        exit_code = 1;
    }
cleanup:
    hermes_platform_shutdown();
    return exit_code;
}
