# Desktop Shell

## Goal

The desktop shell is the operator-facing control surface for Hermes Relay on laptops and workstations.

It exists to make the C core usable in the field without hiding the operational model. The shell should help an operator:

- create and inspect local identities
- import and verify public contacts
- compose short encrypted messages
- inspect and decrypt stored messages for local recipients
- import and export bundle files for manual transfer
- initialize and supervise integrated relay roots
- maintain a relay addressbook
- understand whether the current machine has LAN reachability, Internet reachability, or neither

The desktop shell is not the protocol. It is a local orchestration layer over the protocol implementation.

## Chosen Architecture

The current stack is:

- C core for protocol, crypto, parsing, storage, and relay logic
- shared C ABI for stable desktop and scripting integration
- Python wrapper for orchestration and packaging
- loopback-only local web UI for the operator experience
- optional `pywebview` shell for packaged desktop distribution

This keeps cryptographic correctness and parser safety in the native core while allowing the outer application to evolve faster.

## Workspace Model

The shell now manages a stable local workspace instead of asking the operator to type raw paths for every action.

The workspace contains:

- `identities/`: local private identities
- `contacts/`: imported public contacts
- `stores/`: local envelope stores
- `relays/`: integrated relay roots
- `exports/`: envelopes and bundles prepared for transfer
- `drops/`: reserved staging area for future operator workflows

Default workspace roots:

- macOS: `~/Library/Application Support/HermesRelayDesktop`
- Linux: `${XDG_DATA_HOME:-~/.local/share}/hermes-relay-desktop`
- Windows: `%APPDATA%\\HermesRelayDesktop`

Override with `HERMES_DESKTOP_HOME` or `--workspace-root`.

## Local UI Model

The current UI is intentionally local-only:

- listen address: `127.0.0.1`
- transport: HTTP
- assets: static HTML, CSS, and JavaScript
- backend: Python wrapper

Launch styles:

1. browser shell with `python3 -m hermes_desktop serve-ui --open-browser`
2. embedded `pywebview` shell with `python3 -m hermes_desktop open-webview`

Both launch the same operator interface.

## Current Operator Features

The current shell provides:

- workspace bootstrap and runtime inspection
- best-effort network posture reporting
- identity generation
- contact import
- message creation with configurable PoW difficulty and optional file export
- store listing and decrypt-by-envelope-id
- bundle import and export
- relay initialization
- relay start and stop through the integrated `relay-run` service
- peer addressbook editing
- relay status display
- relay sync, import processing, and export actions
- relay import drop by copying a local file into the relay `import/` directory

The goal is to expose the real operational primitives cleanly rather than simulate a richer model that the protocol does not actually provide.

## Why Not A Pure Browser Site

The shell is local because it needs direct access to:

- local identity files
- local stores and relay roots
- local logs and heartbeat status
- packaged native binaries

This is a desktop control plane, not a hosted SaaS application.

## Why Not A Heavy Native GUI

The current choice avoids a heavier widget toolkit while retaining portability:

- one interface implementation for Windows, macOS, and Linux
- simpler packaging path for a bundled app
- easier inspection and future redesign
- clearer separation between operator workflow and native engine

## Network Guidance Model

The shell provides a best-effort network diagnosis, not an authoritative one.

It currently reports:

- detected non-loopback local addresses
- detected private LAN addresses
- simple Internet reachability probe
- derived mode such as `internet-and-lan`, `lan-only`, or `offline`
- operator advice for the current posture

This is intended to guide transport choice:

- prefer relay peer sync when LAN is healthy
- use Internet relay links when reachable, but continue exporting bundles
- fall back to bundle handoff when disconnected

## Current Limits

The desktop shell is now a practical operator console, but it is still not the final end-user product.

It does not yet provide:

- automatic conversation threading beyond store inspection
- mobile app integration
- Wi-Fi scanning or aggressive roaming logic
- Bluetooth, QR, or LoRa transport adapters
- background desktop notifications

In particular, aggressive Wi-Fi network hopping should be implemented later in a dedicated transport or platform layer rather than improvised inside the current shell.
