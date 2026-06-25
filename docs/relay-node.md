# Relay Node

## Purpose

The integrated relay node is the simplest deployable Hermes Relay package for situations where no richer transport stack is available. It combines:

- a local flat-file envelope store
- a TCP sync listener
- a peer addressbook
- a file-drop import queue
- a local bundle export snapshot

Relevant built-in CLI help:

```sh
./build/hermes-cli help relay-init
./build/hermes-cli help relay-run
./build/hermes-cli help relay-status
```

It is suitable for:

- a LAN relay on a laptop or Raspberry Pi
- an Internet bridge between partially connected areas
- a small always-on relay box
- manual USB or file-drop exchange through the import and export directories

## Layout

`relay-init --root <dir>` creates:

- `store/`: local relay store
- `import/`: operator drop directory for inbound bundle or envelope files
- `archive/`: processed imports moved aside with `.done`
- `export/`: local outbound snapshot bundles
- `contacts/`: reserved for future local contact/addressbook material
- `logs/`: structured JSONL service logs
- `run/`: live service state and PID marker
- `peers.txt`: persisted peer addressbook and health state
- `relay.conf`: listener and interval configuration

## Peer File

The canonical peer file format is tab-separated:

```text
address<TAB>alias<TAB>last_success_unix<TAB>last_attempt_unix<TAB>consecutive_failures<TAB>learned_automatically<TAB>inactive
```

The implementation still accepts the earlier compatibility form `address [alias]` on read and rewrites it into the canonical format on the next save.

`learned_automatically=1` means the peer was discovered from a successful relay handshake rather than manual operator input.

`inactive=1` means repeated sync failures crossed the local threshold. The peer is retained because crisis scenarios assume that nodes may disappear and later return.

## Auto-Learn Policy

By default, the integrated relay keeps a local addressbook and can learn new peers automatically.

The relay only learns a peer after a successful sync session with an explicit claimed listen address in the handshake. It does not trust a raw TCP source port as a reusable relay endpoint.

Learned peers are handled conservatively:

- manual peers are never pruned automatically
- failed peers are marked inactive after repeated failures
- only auto-learned peers that remain inactive for a long stale period are eligible for pruning
- one successful later sync immediately reactivates a returning peer

## Run Loop

`relay-run` performs four jobs in one process:

1. accept inbound TCP sync sessions
2. ingest files found under `import/`
3. sync periodically with peers from `peers.txt`
4. refresh `export/latest.bundle`

With the default `sync_on_change=1`, a successful inbound sync or a newly imported bundle triggers an immediate sync pass to known peers. This provides simple star-like propagation without any central coordinator.

## Service Runtime

The relay process is service-oriented even when run directly in the foreground:

- `run/status.json` is refreshed periodically and can be read with `relay-status`
- `run/relay.pid` prevents two live relay processes from reusing the same root
- `logs/relay.jsonl` stores structured events with local rotation
- `SIGHUP` reopens logs on Unix-like systems
- `SIGTERM` and `SIGINT` stop the relay cleanly

## Security Notes

- config and peer lines that exceed parser limits are rejected
- import files larger than 64 MiB are ignored
- import processing only accepts regular files
- relay retention priority is based on local first-seen time, not sender-supplied freshness
- TCP sync never trusts transport metadata over local envelope verification
- peer address claims are operational metadata only and never affect envelope acceptance
- service logs are structured JSON with escaping and bounded line sizes
- service heartbeat writes use atomic file replacement

## Typical Use

```sh
./build/hermes-cli relay-init --root ./relay-a --listen 0.0.0.0:9440
./build/hermes-cli relay-add-peer --root ./relay-a --peer 192.168.1.44:9440 --alias neighbor
./build/hermes-cli relay-run --root ./relay-a
./build/hermes-cli relay-status --root ./relay-a
```

At startup, `relay-run` prints the root, listen address, peer file, import path, export snapshot path, log path, and heartbeat status path so that operators can validate the runtime layout before leaving the service in the foreground or wrapping it in an OS service manager.

Manual file transfer:

```sh
cp incoming.bundle ./relay-a/import/
./build/hermes-cli relay-imports --root ./relay-a
./build/hermes-cli relay-export-latest --root ./relay-a --out handoff.bundle
```
