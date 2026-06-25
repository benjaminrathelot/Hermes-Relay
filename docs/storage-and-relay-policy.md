# Storage and Relay Policy

## Default Local Policy

The library exposes a configurable `hermes_store_policy` with conservative defaults:

- maximum store bytes: 50 MiB
- maximum envelopes: 25,000
- maximum envelope size: 2,048 bytes
- maximum TTL: 10 days
- minimum PoW difficulty: 28 leading zero bits
- maximum future skew tolerance: 12 hours
- maximum messages per sender per day: 32
- maximum messages per recipient hint per day: 64
- dedup retention: 14 days

These are local policy defaults, not protocol constants except where the protocol specification says otherwise.

## Fixed PoW Threshold

The relay acceptance threshold should stay fixed within a deployment profile. Storage pressure should be handled by:

- hard store caps
- deduplication
- TTL expiry
- weakest-PoW-first eviction
- sender and recipient soft caps

If a relay is overloaded, it should retain less traffic, not silently redefine what counts as a valid sender-computed PoW several hops later.

## Eviction Order

When quota enforcement is needed, the store removes entries in this order:

1. expired envelopes
2. invalid leftovers that fail re-parse
3. weakest PoW envelopes
4. newest locally seen envelopes
5. largest envelopes
6. senders exceeding soft local caps

The implementation scores envelopes so stronger PoW, earlier `first_seen_at_local`, and smaller size are retained preferentially. This prevents a late flood of fresh traffic from evicting older envelopes that were already waiting for carriage.

## Why Retain Seen Markers Longer Than Message TTL

The relay TTL is capped at 10 days, but seen markers should survive slightly longer. This protects against delayed replay from stale bundles that re-enter the network near expiry boundaries.
