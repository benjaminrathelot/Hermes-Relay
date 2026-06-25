# Emergency Relay Playbook

## Purpose

This document describes how to operate Hermes Relay nodes and operator workstations when infrastructure is degraded, intermittent, or partitioned.

It is intentionally practical. It assumes:

- very short message traffic
- partial trust in the surrounding environment
- intermittent or absent Internet
- manual intervention is acceptable
- relays may disappear and return later

## Operating Modes

### 1. Local LAN Cell

Use this when several operators or relay boxes share the same Wi-Fi or wired segment.

Recommended pattern:

- run at least one integrated relay on stable power
- configure peers explicitly with `relay-add-peer`
- keep one or more operator workstations able to export bundles manually
- prefer fixed local addresses when possible

This is the lowest-friction mode when local infrastructure still exists.

### 2. Bridged Islands

Use this when one site still has occasional Internet but others do not.

Recommended pattern:

- run an integrated relay in each site
- let the bridge site maintain peer links to other reachable bridge nodes
- continue exporting bundles at each site for physical fallback
- do not rely on a single Internet path as the only route

The bridge node is a convenience, not a trust anchor.

### 3. Physical Ferry

Use this when there is no live inter-site network path.

Recommended pattern:

- export a bundle from the local store or relay
- carry the bundle on a phone, laptop, USB device, or printed QR chunks later
- import it into the next reachable store or relay
- repeat opportunistically

Do not move raw internal store directories between systems. Move bundle files only.

## Relay Placement Guidance

Prefer relay hosts that are:

- on stable power
- physically secure enough to avoid trivial tampering
- reachable by multiple local operators
- simple to recover or replace
- not the only copy point for a traffic island

Good candidates:

- small relay box on Ethernet or hotspot power
- workstation left running in a coordination room
- low-power single-board computer with enough storage and power budget

## Addressbook Strategy

The integrated relay keeps a persistent peer addressbook.

Operational guidance:

- add known relays explicitly before crisis conditions when possible
- keep peers even if they disappear temporarily
- mark or treat repeatedly failing peers as degraded, not immediately malicious
- avoid aggressive peer deletion during unstable conditions

A relay disappearing and returning later is normal in this threat and outage model.

## File Handoff Strategy

When file transfer becomes necessary:

- export bundles frequently enough to avoid losing newly accepted traffic
- label media physically if several ferry routes exist
- import received bundles promptly, then re-export from the receiving relay if onward transport is needed
- preserve the most recent successful bundles, but do not accumulate endless historical copies on constrained systems

Bundle handoff is the safest transport fallback because it preserves opaque envelopes and avoids direct manipulation of relay internals.

## Store And Capacity Discipline

Under crisis load:

- keep relay quotas bounded
- run cleanup regularly
- prefer smaller recent envelopes with valid PoW
- monitor store growth and active peer count
- assume spam attempts are possible

Messages are equal in priority at the protocol level. Operational priority should come from keeping the network healthy, not from inventing out-of-band per-message urgency flags.

## Current Desktop-Shell Boundaries

The current desktop shell helps with:

- local identity and contact management
- composing and decrypting messages
- bundle import and export
- integrated relay service control
- peer addressbook edits
- basic network posture reporting

It does not yet provide:

- automatic Wi-Fi roaming
- Bluetooth transport
- mobile-specific transport logic
- unattended QR chunk workflows

Operators should plan with those limits in mind.
