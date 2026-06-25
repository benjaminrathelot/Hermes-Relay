# Service Operations

## Scope

This document covers the integrated Hermes Relay service runtime that is built into `hermes-cli relay-run`.

The runtime is intentionally foreground-first:

- the relay process owns the run loop
- structured logging is handled inside the process
- log rotation is handled inside the process
- a heartbeat status file is written inside the relay root
- graceful stop is handled through the platform abstraction

That design keeps the operational core identical across platforms. External service managers are wrappers around the same foreground runtime.

## Service Layout

`relay-init --root <dir>` now creates and uses:

- `store/`: persisted envelopes and dedup markers
- `import/`: local operator drop directory
- `archive/`: processed inbound files
- `export/`: latest outbound snapshot bundle
- `contacts/`: reserved for future local contact state
- `logs/`: structured relay logs
- `run/`: service runtime state
- `run/status.json`: current heartbeat state
- `run/relay.pid`: live process marker while the relay is running

## Heartbeat State

`relay-status --root <dir>` prints `run/status.json`.

The status file is rewritten atomically and contains:

- `state`
- `ts_unix`
- `pid`
- `listen_addr`
- `started_at_unix`
- `last_sync_at_unix`
- `last_import_at_unix`
- `last_export_at_unix`
- `last_cleanup_at_unix`
- `inbound_sessions`
- `auto_learn_events`
- `sync_failures`
- `import_failures`
- `store_envelopes`
- `store_bytes`
- `expired_envelopes`
- `peer_count`
- `active_peers`
- `inactive_peers`

Operators should treat the status file as local truth for service liveness, not sender-supplied envelope metadata.

## Structured Logs

The service writes JSON Lines to `log_path`, which defaults to `logs/relay.jsonl`.

Each line contains:

- `ts_unix`
- `level`
- `component`
- `event`
- event-specific typed fields

Current service events include:

- `service_started`
- `service_stopped`
- `log_reopened`
- `imports_processed`
- `import_rejected`
- `sync_pass`
- `inbound_session_failed`
- `peer_auto_learned`

The logger escapes control characters and writes line-buffered JSON. Log rotation is local and size-based:

- `log_rotate_bytes`: rotate threshold
- `log_rotate_keep`: number of retained historical files

Rotated files use:

- `relay.jsonl`
- `relay.jsonl.1`
- `relay.jsonl.2`
- and so on

## Startup And Shutdown

Recommended steady-state operation:

```sh
./build/hermes-cli relay-run --root ./relay-a
```

Useful runtime overrides:

```sh
./build/hermes-cli relay-run \
  --root ./relay-a \
  --listen 0.0.0.0:9440 \
  --heartbeat-interval 15 \
  --log-rotate-bytes 16777216 \
  --log-rotate-keep 8
```

On Unix-like systems:

- `SIGTERM`: graceful stop
- `SIGINT`: graceful stop
- `SIGHUP`: reopen the log file

The relay clears `run/relay.pid` on clean shutdown and leaves `run/status.json` with `state="stopped"`.

## Recovery Model

Recovery is based on persisted local state, not on an external coordinator.

After a crash or reboot:

1. restart the same relay root
2. the store is reopened from disk
3. peers are reloaded from `peers.txt`
4. imports still present in `import/` are retried
5. exports are regenerated on schedule

If a stale PID file exists, startup will:

- read the recorded PID
- check whether that process still exists
- reuse the root if the PID is stale
- refuse startup if another live process is still using the same root

## Cross-Platform Deployment

The foreground runtime is platform-neutral in behavior. External wrappers differ by OS:

- Linux: `systemd` unit under `packaging/linux/`
- macOS: `launchd` plist under `packaging/macos/`
- Windows: WinSW wrapper configuration under `packaging/windows/`

Windows note:

The repository now has an explicit platform layer for sockets, PID files, directory walking, and service control. The Windows wrapper and Win32 backend are therefore aligned with the code structure. However, Windows still needs native build, soak, and operator validation before it should be called production-ready to the same level as Linux and macOS.

## Operational Checklist

Before field deployment:

1. run `relay-init`
2. add at least one peer manually
3. confirm `relay-status` shows `running`
4. confirm `logs/relay.jsonl` is being written
5. confirm `export/latest.bundle` is refreshed
6. confirm `import/` ingestion works with a known-good bundle
7. confirm a stop and restart preserve queued envelopes

Under incident pressure:

1. check `relay-status`
2. inspect `logs/relay.jsonl` for `sync_pass` and `import_rejected`
3. inspect `peers.txt` for peers marked `inactive=1`
4. verify local disk space before increasing store policy or log retention

## Limits

This runtime materially improves deployability and recovery, but it is still not a substitute for:

- external security audit
- long-duration soak testing on every target OS
- staged upgrade rehearsals
- operator training on local failure procedures
