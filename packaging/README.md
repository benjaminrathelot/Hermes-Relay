# Packaging

The relay runtime is intentionally a foreground process. Packaging therefore wraps the same binary on each OS rather than changing service behavior by platform.

Provided assets:

- `linux/hermes-relay@.service`: `systemd` instance unit
- `macos/org.hermesrelay.node.plist`: `launchd` property list
- `windows/hermes-relay-service.xml`: WinSW wrapper template

Validation status:

- Linux: service wrapper provided and aligned with the tested foreground runtime
- macOS: service wrapper provided and aligned with the tested foreground runtime
- Windows: wrapper template and Win32 platform backend provided, but native validation is still required before it should be called production-ready on Windows

Operational model:

1. install `hermes-cli`
2. run `relay-init --root <dir>`
3. add peers
4. run the OS wrapper against `relay-run --root <dir>`

The package assembly script is `scripts/package.sh`.
