# Operator Quickstart

## Scope

This guide is for first deployment and first use.

It assumes:

- one operator workstation
- one or more recipient contacts
- optional relay nodes for LAN or bridge service
- file, USB, or LAN exchange as the immediate transport

## 1. Create Identities

Each user creates one private identity locally:

```sh
./build/hermes-cli genid --identity ./alice.id --alias alice
./build/hermes-cli export-contact --identity ./alice.id --out ./alice.contact
./build/hermes-cli fingerprint --identity ./alice.id
```

Do not distribute `alice.id`. Only distribute `alice.contact`.

## 2. Exchange Contacts Safely

Before trusting a contact, verify the fingerprint through an independent channel such as voice, in-person confirmation, or previously trusted paper records.

Then normalize the received file locally:

```sh
./build/hermes-cli import-contact --in /media/usb/bob.contact --out ./contacts/bob.contact
./build/hermes-cli fingerprint --contact ./contacts/bob.contact
```

## 3. Create One Message

```sh
./build/hermes-cli create \
  --identity ./alice.id \
  --contact ./contacts/bob.contact \
  --store ./alice-store \
  --message "Meet at well 18:00" \
  --out ./msg.bin
```

This produces:

- a locally stored envelope in `./alice-store`
- an optional portable envelope file `./msg.bin`

The envelope is already encrypted, signed, and bound to sender-side proof of work.

## 4. Move Traffic Offline

For one message:

- carry the envelope file directly

For repeated or multi-message transfer:

- use bundles

Example:

```sh
./build/hermes-cli export-bundle --store ./alice-store --out ./handoff.bundle
./build/hermes-cli import-bundle --store ./relay-store --in ./handoff.bundle
```

Bundles are intended for:

- USB keys
- local file copy
- partial Internet upload when available
- later QR chunking gateways

## 5. Read A Message

The recipient should first verify, then decrypt:

```sh
./build/hermes-cli verify --envelope ./msg.bin
./build/hermes-cli decrypt --identity ./bob.id --envelope ./msg.bin
```

Verification is safe on untrusted input. Decryption should only happen on the intended recipient endpoint.

## 6. Bring Up A Relay

Create the relay root:

```sh
./build/hermes-cli relay-init --root ./relay-a --listen 0.0.0.0:9440
```

Add one or more peers:

```sh
./build/hermes-cli relay-add-peer --root ./relay-a --peer 192.168.1.44:9440 --alias neighbor
./build/hermes-cli relay-add-peer --root ./relay-a --peer 198.51.100.10:9440 --alias bridge-west
```

Run the service:

```sh
./build/hermes-cli relay-run --root ./relay-a
```

Inspect health:

```sh
./build/hermes-cli relay-status --root ./relay-a
./build/hermes-cli relay-peers --root ./relay-a
```

## 7. Use The Relay As A Local Handoff Point

Queue an inbound file:

```sh
./build/hermes-cli relay-drop --root ./relay-a --in ./handoff.bundle
```

If the long-running relay service is not active, process imports manually:

```sh
./build/hermes-cli relay-imports --root ./relay-a
```

Produce an outbound handoff snapshot:

```sh
./build/hermes-cli relay-export-latest --root ./relay-a --out ./handoff-out.bundle
```

## 8. First Production Checks

Before trusting a relay in a crisis workflow:

1. confirm `relay-status` reports `running`
2. confirm `logs/relay.jsonl` is being written
3. confirm `export/latest.bundle` changes after imports or sync
4. verify a known-good bundle can traverse one full round trip
5. restart the relay and confirm state recovery from disk

## 9. What Not To Do

- do not trust sender timestamps as local truth
- do not raise protocol TTL above the V1 maximum
- do not copy private identity files onto shared relays
- do not transmit raw internal store files as an interoperability format
- do not assume Internet availability or stable peer uptime

## 10. Next Documents

After this quickstart, the next useful references are:

- `docs/cli-guide.md`
- `docs/relay-node.md`
- `docs/service-operations.md`
- `docs/storage-and-relay-policy.md`
