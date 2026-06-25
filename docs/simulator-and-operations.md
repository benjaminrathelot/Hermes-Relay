# Simulator and Operations

## Simulator Model

The simulator models:

- randomly connected peers
- disconnected islands
- intermittent contact windows
- PoW-weighted storage retention
- spam injections
- relay expiry after 10 days

The model is intentionally simple. It is meant to compare policy choices, not to predict real-world propagation exactly.

## Operational Notes

- LAN sync is safe to use on trusted local networks, but plaintext secrecy does not rely on transport secrecy.
- Bundle export is intended for USB, file copy, and QR chunking gateways.
- Relays should publish their local relay mode policy to operators, not to peers.
- Crisis mode should increase storage and peer discovery frequency, not change protocol validation rules.

