# Hermes Desktop Wrapper

This directory contains the Python desktop-oriented wrapper for Hermes Relay.

It is split into four layers:

- `binding.py`: loads the shared `libhermesrelay` ABI with `ctypes`
- `core.py`: high-level Python access to identities, messages, stores, bundles, and relay helpers
- `workspace.py`: stable operator workspace layout for identities, contacts, stores, relays, and exports
- `web_local.py`: loopback-only HTTP control plane and local operator UI

The wrapper stays conservative by design:

- no cryptographic logic is reimplemented in Python
- no network listener other than `127.0.0.1` is used for the UI
- the long-running relay service remains the native `hermes-cli relay-run` process

Optional desktop shell:

- `webview_app.py` opens the same local UI inside `pywebview`

## Typical Development Use

From the repository root:

```sh
./scripts/build.sh
PYTHONPATH=python/src python3 -m hermes_desktop info
PYTHONPATH=python/src python3 -m hermes_desktop serve-ui --open-browser
```

Useful flags:

- `--workspace-root PATH`: choose the operator workspace location explicitly
- `--host 127.0.0.1 --port 8765`: bind the loopback UI

## What The Current Shell Provides

The desktop shell currently supports:

- workspace bootstrap
- identity generation
- contact import
- short-message creation
- store inspection and message decryption
- bundle import and export
- relay initialization and service supervision
- peer addressbook updates
- relay sync, import processing, and latest-bundle export
- best-effort local network posture reporting

It is meant to feel like a practical operator console, not a thin raw API demo.

## Packaging Direction

The intended packaging path remains:

1. build `libhermesrelay` and `hermes-cli`
2. vendor this Python package and its assets
3. bundle the Python app plus native binaries with PyInstaller
4. optionally wrap the loopback UI in `pywebview`

This allows packaged distribution without requiring the operator to install Python manually.
