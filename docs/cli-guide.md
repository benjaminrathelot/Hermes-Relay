# CLI Guide

## Purpose

`hermes-cli` is the operator-facing entry point for the Hermes Relay C core.

It covers four practical jobs:

- generate and manage identities
- create and inspect encrypted envelopes
- import and export offline bundles
- run and operate the integrated relay service

The CLI is intentionally explicit. Commands do one thing each and expose file paths directly so that operators can script them, inspect outputs, and move artifacts through degraded environments without hidden state.

## Built-In Help

The CLI ships with integrated command help:

```sh
./build/hermes-cli --help
./build/hermes-cli help create
./build/hermes-cli relay-run --help
```

Use the built-in help for exact flags. Use this document for operational context.

## Command Families

### Identity And Contact Commands

- `genid`: create a new local identity
- `fingerprint`: print the public fingerprint and recipient hint
- `export-contact`: derive a shareable public contact file from an identity
- `import-contact`: normalize a received contact file into local storage

Typical sequence:

```sh
./build/hermes-cli genid --identity ./alice.id --alias alice
./build/hermes-cli export-contact --identity ./alice.id --out ./alice.contact
./build/hermes-cli fingerprint --identity ./alice.id
```

### Message Commands

- `create`: encrypt, sign, PoW-protect, and store one message
- `verify`: parse and verify one serialized envelope
- `decrypt`: decrypt one envelope for its intended recipient

Typical sequence:

```sh
./build/hermes-cli create \
  --identity ./alice.id \
  --contact ./bob.contact \
  --store ./alice-store \
  --message "Meet at well 18:00" \
  --out ./msg.bin

./build/hermes-cli verify --envelope ./msg.bin
./build/hermes-cli decrypt --identity ./bob.id --envelope ./msg.bin
```

### Store And Transfer Commands

- `import-bundle`: ingest a bundle into a local store
- `export-bundle`: write the forwardable store contents into one bundle
- `serve`: expose a single store over the TCP sync transport
- `sync`: synchronize one store with one peer
- `stats`: inspect local store pressure and inventory
- `cleanup`: expire forward-invalid traffic and apply store hygiene immediately

Typical offline handoff:

```sh
./build/hermes-cli export-bundle --store ./relay-store --out ./handoff.bundle
./build/hermes-cli import-bundle --store ./other-store --in ./handoff.bundle
```

### Integrated Relay Commands

- `relay-init`: create a complete relay root
- `relay-add-peer`: add or update a peer in the relay addressbook
- `relay-peers`: inspect current peer state
- `relay-drop`: queue an inbound file into `import/`
- `relay-imports`: process queued imports manually
- `relay-export-latest`: refresh and copy the latest outbound snapshot
- `relay-run`: start the integrated foreground relay service
- `relay-status`: print the heartbeat JSON document

Typical relay sequence:

```sh
./build/hermes-cli relay-init --root ./relay-a --listen 0.0.0.0:9440
./build/hermes-cli relay-add-peer --root ./relay-a --peer 192.168.1.44:9440 --alias neighbor
./build/hermes-cli relay-run --root ./relay-a
```

Integrated relay operating model:

- `relay-init` creates the relay layout, config file, peer file, import area, export area, logs, and runtime directory
- `relay-run` is the foreground service process and should be the normal operating mode
- `relay-status` is the fastest health check because it reads the atomically rewritten heartbeat file
- `relay-add-peer` edits the persistent addressbook rather than a transient in-memory list
- `relay-imports` is the manual catch-up tool for bundles or envelopes dropped into `import/`
- `relay-export-latest` prepares a fresh bundle snapshot for manual ferry or one-way handoff

The relay is designed for store-carry-forward environments. A peer going silent does not mean it should be deleted immediately.

## Files And Artifacts

The CLI works with a small set of stable artifact types:

- identity file: private local identity, never share it
- contact file: public recipient material, safe to share
- envelope file: one wire-format encrypted message
- bundle file: many envelopes packed for transport
- relay root: integrated relay working directory

For the integrated relay, the most important subpaths are:

- `relay.conf`: relay configuration
- `peers.txt`: persistent addressbook
- `import/`: inbound bundle or envelope drop zone
- `export/latest.bundle`: most recent exported snapshot
- `logs/`: structured rotated service logs
- `run/status.json`: heartbeat status document
- `run/relay.pid`: local process lock file

Operators should move contact files, envelope files, and bundle files as opaque artifacts. Plaintext is only present at message creation time and at recipient decryption time.

## Exit Discipline

The CLI exits non-zero on invalid arguments, parse failure, cryptographic failure, proof-of-work failure, quota rejection, or file I/O failure.

For automated use:

- inspect the exit code first
- then inspect standard error
- then inspect relay logs if the failing command targeted an integrated relay

## Recommended Operator Habits

- verify contact fingerprints out of band before first use
- verify envelopes before attempting decryption
- keep private identities off shared relay hosts
- prefer `relay-run` over ad hoc `serve` for real operations
- use bundles for USB and file transfer rather than copying internal store files directly
- read `relay-status` and structured logs before changing relay policy
- keep at least one export path available even when peer sync looks healthy
- treat disappearing peers as expected in crisis conditions unless other evidence suggests compromise
- use the desktop shell for day-to-day operator control, but keep the CLI available for low-level recovery
