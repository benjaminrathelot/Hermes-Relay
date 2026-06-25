# Hermes Relay Threat Model

## Security Goals

Hermes Relay V1 attempts to provide:

- end-to-end confidentiality for message plaintext
- sender authentication
- integrity protection for the entire envelope
- practical relay-side spam resistance
- resilience to hostile or unreliable transports
- safe handling of malformed inputs

## Assumptions

- relays are untrusted
- endpoints may be intermittently connected
- attackers may generate unlimited keypairs
- attackers may replay valid envelopes until expiry
- attackers may operate modified clients
- some links may be monitored
- local device compromise is out of scope once keys are stolen

## Adversaries

### Opportunistic passive observer

Can observe LAN traffic, copied bundles, QR transfers, or stored envelope files.

Mitigations:

- no plaintext in transport
- no dependence on TLS for confidentiality
- recipient full key is not exposed in routing metadata

### Active malicious relay

Can drop, replay, reorder, delay, or selectively forward envelopes.

Mitigations:

- end-to-end encryption
- signatures verified by relays and recipients
- envelope hashes and identifiers used for deduplication
- no trust in transport metadata

### Bulk spammer

Can create unlimited keys and flood the network.

Mitigations:

- sender-paid proof of work
- hard envelope size cap
- maximum relay TTL
- local quotas
- deduplication
- stronger local minimum difficulty under storage pressure
- eviction that preferentially removes weak PoW traffic

### Malformed input attacker

Can deliver corrupted or malicious files, bundles, or network frames.

Mitigations:

- canonical parser
- explicit lengths
- bounds checks
- reject oversized objects before allocation
- fuzz target for parser

## Important Non-Goals

- perfect metadata privacy
- resistance to global traffic analysis
- sender deniability
- forward secrecy against endpoint key compromise after receipt
- unstoppable delivery

## Key Risks and Operational Caveats

### Metadata leakage

Relays can observe:

- sender public identity
- recipient hint
- timing
- size
- PoW strength

The design reduces but does not eliminate metadata exposure.
This is a conscious V1 tradeoff, not an accidental leak.

### Physical capture

If a device is captured and its identity file is readable, confidentiality and sender authenticity for future traffic are lost. Operators should protect identity files with operating-system permissions and external full-disk encryption where available.

### Replay within TTL

Valid envelopes can be replayed until they expire. Deduplication records limit the impact but do not prevent repeated transport attempts by malicious peers.

### False timestamps

Senders control `created_at_unix` and `expires_at_unix`, so those values are not trusted as proofs of time. Hermes Relay mitigates this by:

- rejecting envelopes too far in the future relative to local time
- recording local first-seen time
- capping forwarding lifetime by local first-seen plus protocol TTL

### Local denial of service

An attacker can still consume local CPU and disk budget by presenting large volumes of near-threshold traffic. Hermes Relay only raises the cost; it does not eliminate resource exhaustion.
