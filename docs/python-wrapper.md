# Python Wrapper

## Purpose

The Python wrapper is the bridge between the portable C core and a future desktop-oriented user interface.

It exists for three reasons:

- expose a stable high-level API to Python without reimplementing protocol logic
- support a local operator UI built with ordinary web technologies
- prepare packaging paths where end users do not need a separate Python installation

## Design

The wrapper is intentionally split into two control planes.

### Direct Native ABI

The wrapper loads the shared native library through the stable C ABI declared in:

- `include/hermes/api.h`

This ABI exposes safe high-level operations such as:

- generate identity and contact files
- inspect identities and contacts
- create, verify, and decrypt short messages
- import and export bundles
- inspect store stats and inventory
- initialize relay roots
- add and list relay peers
- process relay imports
- export the relay snapshot
- run one relay sync pass
- read relay heartbeat status

The Python code does not bind directly to internal structs like `hermes_envelope` or `hermes_store`.

### Relay Supervision

The long-running relay service remains the existing foreground process:

- `hermes-cli relay-run`

That choice keeps one service runtime implementation for:

- CLI operators
- service wrappers
- the Python desktop shell

The Python wrapper supervises that process rather than reimplementing the run loop in Python.

## Why This Model

This model keeps critical behavior inside the auditable C core:

- parsing
- cryptography
- proof-of-work validation
- store policy
- relay acceptance

Python handles the product shell:

- local UI
- orchestration
- packaging glue
- future desktop workflow integration

## Local Web UI

The first desktop-oriented shell is intentionally modest:

- local-only HTTP listener on `127.0.0.1`
- simple HTML/CSS/JS control surface
- optional `pywebview` wrapper for an app window

This keeps the UI stack lighter than a Qt desktop application while preserving portability across desktop systems.

## Security Constraints

The local Python shell must obey the following rules:

- bind the UI to loopback only by default
- never expose the local control plane on `0.0.0.0`
- never replace native cryptographic checks with Python logic
- treat all imported files and paths as untrusted inputs
- keep relay transport listeners separate from local UI listeners

## Packaging Direction

The desktop package should eventually ship:

- native `libhermesrelay`
- `hermes-cli`
- Python wrapper runtime
- local UI assets
- optional `pywebview` shell

The operator should launch a packaged desktop app, not assemble Python manually.
