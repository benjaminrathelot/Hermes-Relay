# Hermes Relay Protocol Specification V1

## 1. Scope

Hermes Relay V1 defines a transport-agnostic envelope for very short, end-to-end encrypted text messages that can move through disconnected or partially connected networks. The protocol is designed for opportunistic forwarding, local storage, physical carriage, and cheap relay-side validation.

The protocol does not provide:

- message ordering
- delivery guarantees
- groups
- attachments
- anonymity
- central account recovery

## 2. Normative Terms

The words must, should, and may are used in the RFC 2119 sense.

## 3. Interoperable Cryptographic Suite

V1 fixes one suite for interoperability:

- `crypto_suite_id = 1`
- signature algorithm: Ed25519
- asymmetric encryption: X25519
- key derivation: HKDF-SHA256
- authenticated encryption: ChaCha20-Poly1305 with a 96-bit nonce
- hash function: SHA-256
- PoW hash: SHA-256

All compliant V1 instances must support this suite.

## 4. Public Identity Format

Hermes Relay uses a two-key public identity bundle:

- `sig_public[32]`: Ed25519 public key
- `box_public[32]`: X25519 public key

When the specification refers to `sender_public_key`, it means the 64-byte concatenation:

```text
sender_public_key = sig_public || box_public
```

The `recipient_hint` is:

```text
recipient_hint = SHA-256(recipient_sig_public || recipient_box_public)[0..19]
```

The full recipient public identity is not exposed in routing metadata.

The sender public identity is exposed in routing metadata in V1. This is an explicit tradeoff in favor of simple relay validation and interoperable sender authentication. V1 does not attempt sender-privacy against relays.

## 5. Envelope Limits

- plaintext maximum: 1024 bytes
- recommended plaintext maximum: 512 bytes
- wire-format maximum: 2048 bytes
- maximum relay TTL in V1: 10 days
- default TTL: 10 days

Expired envelopes must not be forwarded.

## 6. Canonical Envelope Layout

All integers are unsigned big-endian.

```text
offset  size  field
0       4     magic = "HRM1"
4       1     protocol_version
5       1     crypto_suite_id
6       1     pow_algorithm
7       1     reserved_header
8       2     flags
10      2     reserved_future_fields_length
12      16    envelope_id
28      8     created_at_unix
36      8     expires_at_unix
44      64    sender_public_key
108     20    recipient_hint
128     2     payload_size
130     N     encrypted_payload
130+N   64    signature
194+N   4     pow_difficulty
198+N   8     pow_nonce
206+N   M     reserved_future_fields
```

Where:

- `pow_algorithm = 1` means SHA-256 leading-zero-bit proof of work
- `flags` is a bitmask defined by local applications
- `reserved_future_fields_length = M`
- `payload_size = N`

The total encoded size must not exceed 2048 bytes.

## 7. Encrypted Payload Format

For suite `1`, the `encrypted_payload` is:

```text
offset  size  field
0       32    ephemeral_x25519_public
32      2     plaintext_size
34      P     ciphertext
34+P    16    poly1305_tag
```

Where `P = plaintext_size`.

The plaintext is an uninterpreted UTF-8 text octet string at the protocol level. Applications may reject invalid UTF-8.

The associated data for AEAD is:

```text
"HRM-AD-V1" ||
protocol_version ||
crypto_suite_id ||
flags ||
created_at_unix ||
expires_at_unix ||
sender_public_key ||
recipient_hint
```

For suite `1`, the 96-bit ChaCha20-Poly1305 nonce is not randomly reused across messages. It is deterministically derived from the ephemeral X25519 shared secret and HKDF context. Nonce safety therefore reduces to ephemeral key uniqueness and correct HKDF domain separation rather than to caller-managed random nonce bookkeeping.

## 8. Envelope Identifier

The sender computes the envelope core preimage over all fields except:

- `magic`
- `envelope_id`
- `signature`
- `pow_nonce`

The exact core preimage is:

```text
protocol_version ||
crypto_suite_id ||
pow_algorithm ||
flags ||
created_at_unix ||
expires_at_unix ||
sender_public_key ||
recipient_hint ||
payload_size ||
encrypted_payload ||
pow_difficulty ||
reserved_future_fields_length ||
reserved_future_fields
```

Then:

```text
envelope_id = SHA-256("HRM-EID-V1" || core_preimage)[0..15]
```

This makes the identifier stable across repeated PoW attempts for the same message.
It also ensures that future extension bytes cannot create identifier collisions with otherwise identical V1 envelopes.

## 9. Signature Preimage

The sender signs the canonical signature preimage:

```text
magic ||
protocol_version ||
crypto_suite_id ||
pow_algorithm ||
reserved_header ||
flags ||
reserved_future_fields_length ||
envelope_id ||
created_at_unix ||
expires_at_unix ||
sender_public_key ||
recipient_hint ||
payload_size ||
encrypted_payload ||
pow_difficulty ||
reserved_future_fields
```

The signature field and `pow_nonce` are excluded.

Relays must verify the signature before accepting or forwarding an envelope.

## 10. Proof of Work

The PoW preimage is:

```text
protocol_version ||
sender_public_key ||
recipient_hint ||
created_at_unix ||
expires_at_unix ||
SHA-256(encrypted_payload) ||
signature ||
pow_algorithm ||
pow_difficulty ||
pow_nonce
```

For `pow_algorithm = 1`, validity means the SHA-256 digest has at least `pow_difficulty` leading zero bits.

This is equivalent to:

```text
SHA-256(pow_preimage) < target_for_difficulty
```

But the leading-zero-bit form avoids multi-precision arithmetic in small portable implementations.

The PoW is:

- computed once by the sender
- cheaply verifiable
- bound to sender, recipient hint, payload, timestamps, and signature
- invalidated by any meaningful message change
- non-transferable to another message

For interoperable offline operation, the required relay-acceptance PoW threshold should be fixed for a deployment profile, not raised opportunistically by later relays based on storage pressure. Otherwise a sender cannot know what work level will remain acceptable after several store-carry-forward hops.

This repository's V1 default deployment profile uses `28` leading zero bits.

## 11. Relay Acceptance Rules

A relay accepts an envelope only if:

1. canonical parse succeeds
2. encoded size is at most 2048 bytes
3. the envelope is not expired
4. `expires_at_unix - created_at_unix` is at most 10 days
5. `created_at_unix` is not later than local time plus local future-skew tolerance
6. `envelope_id` has not already been seen
7. the signature is valid
8. the PoW is valid
9. local policy allows storage

The sender-supplied timestamps are not authoritative. Without an external trusted time source, no offline protocol can prove absolute wall-clock truth from message contents alone. V1 therefore requires each relay to treat timestamps as untrusted claims and record `first_seen_at_local` when an envelope is first accepted.

Forwarding validity is then bounded by:

```text
local_relay_deadline = min(expires_at_unix, first_seen_at_local + 10 days)
```

An envelope must not be forwarded after `local_relay_deadline`, even if the sender supplied a later-looking timestamp that still passed future-skew validation.

Under storage pressure, relays should base retention priority on local acceptance state such as `first_seen_at_local`, not on sender-supplied recency claims alone. This prevents newer traffic, including bounded future-skew traffic, from displacing older queued envelopes simply by appearing fresher.

## 12. Storage Semantics

Each device should maintain deduplication records for at least 14 days:

- seen envelope identifiers
- seen envelope hashes

Expired envelopes may be retained locally for operator reasons, but expired envelopes must never be exported for relay or forwarded to peers.

## 13. Sync Model

When two peers meet:

1. exchange compact inventories of envelope identifiers
2. compute missing identifiers
3. request missing envelopes
4. verify each envelope locally
5. store valid envelopes
6. never transmit plaintext
7. never require sender or recipient to be online

## 14. Forward Compatibility

`reserved_future_fields` is a raw byte string. V1 relays:

- must preserve it verbatim during store, export, import, and relay
- must not interpret it unless a higher version defines semantics
- may reject envelopes whose reserved length causes total size to exceed limits
