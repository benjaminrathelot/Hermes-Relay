# Hermes Relay Architecture

## Components

### Public API

The library is organized around a small number of public modules:

- `identity`: key generation, contact export, contact parsing, fingerprints
- `crypto`: signatures, sealed payload encryption, decryption, hashing, PoW
- `envelope`: canonical wire encoding, parsing, signing, verification
- `store`: flat-file object store, dedup markers, quota enforcement
- `bundle`: bundle import and export for offline transfer
- `sync`: inventory exchange and request planning
- `transport`: LAN TCP sync helper

### Platform Boundary

The implementation is intentionally split in three layers:

- `core`: protocol, crypto abstraction, canonical serialization, store policy, bundle logic, relay policy
- `platform`: narrow OS API for process lifecycle, directory walking, file replacement, PID files, sockets, and service signals
- `backend implementation`: `platform_posix.c` for Linux, macOS, and Android-class POSIX targets, and `platform_win32.c` for Windows

That means envelope format, PoW validation, relay policy, storage rules, and sync semantics do not change by operating system. Only the small platform layer is allowed to talk directly to OS-specific APIs.

The current public header for that boundary is `include/hermes/platform.h`.

### CLI

The CLI is intentionally narrow and operational:

- key management
- contact exchange
- message creation
- verification and decryption
- bundle transfer
- TCP relay service
- ad hoc peer synchronization
- store maintenance

### Simulator

The simulator is independent from the production store and transport implementation. It models epidemic forwarding, storage pressure, and PoW-weighted retention over time.

## Backend Choices

### Crypto

The active backend uses OpenSSL 3 EVP APIs. The wire format is fixed independently of the backend so an alternative libsodium implementation can be added later.

The backend boundary is intentionally narrow: identity generation, hashing, signatures, sealed payload encryption, decryption, and PoW hashing are abstracted from the envelope and store code so a libsodium backend can replace the OpenSSL implementation without wire-format changes.

Relay pressure handling is intentionally separated from protocol validity. The store may evict weaker traffic first, but it should not reinterpret a previously valid envelope's sender-computed PoW based on local disk pressure alone.

### Platform

The transport layer now uses platform socket wrappers rather than raw POSIX calls, and the relay runtime uses the same abstraction for:

- service stop and log-reopen signals
- directory iteration
- PID locking
- atomic file replacement
- listener accept loops

This is the portability seam for future Windows validation, Android packaging, and embedded relay builds. Android currently follows the POSIX backend model.

### Storage

The default store is file-system based. This keeps the core portable and easy to audit:

- one file per accepted envelope under `objects/`
- seen markers under `meta/seen-id/` and `meta/seen-hash/`
- no database daemon
- simple directory replication and backup

SQLite can be added later behind the same store API.
