# Replication

📦 One carrier-agnostic replication runtime for Project-Networking. Photon Realtime is the carrier in
use; Epic Online Services and PlayFab Party are written as links and refuse to open until their SDKs
are linked. Nothing above the link names a carrier.

## The four pieces

| File | What it is |
| --- | --- |
| `Source/ReplicationLink.h` | The seam: `ReplicationCarrier`, `LinkStanding`, `LinkReading`, `LinkSpecification`, `ArrivedPacket`, and the `ReplicationLink` a carrier implements |
| `Source/ReplicationLink.cpp` | The in-process loopback carrier, the two carriers that are seams, and `OpenReplicationLink` / `ResolveCarrier` |
| `Source/PhotonReplicationLink.cpp` | Photon Realtime, over the existing `PhotonTransport`. No Photon header is included here, so it links the same against the real SDK or `PhotonLinkStub.cpp` |
| `Source/ReplicatedPlacement.h` | `PropertyLayout`, `ReplicatedProperty`, `ReplicatedDescription`, `DescriptionSignature`, `PropertyMoved` |
| `Source/ReplicationSequence.h/.cpp` | The runtime: enrolment, authority, change detection, snapshot encode/decode, easing |

## Adding a carrier

Implement `ReplicationLink` in one new `.cpp`, add the enumerant, and name it in
`OpenReplicationLink`. Nothing else changes — the sequence, the wire shape and the proof are all
carrier-free. `ResolveCarrier` walks the preference order and never resolves to a carrier that
reports itself unavailable.

## The wire

A snapshot body rides inside a `PacketCodec` frame of `ReplicationKind::Snapshot`:

```
'S' | full u8 | moment u32BE | count u16BE
then per placement:  identity u32BE | signature u16BE | moved u32BE | the moved bytes, in order
```

- `moved` is a 32-bit run, so a description carries at most 32 properties; a wider one is refused at
  enrolment rather than silently clipped.
- `signature` is FNV-1a over the property order, layouts and offsets — never the names. Renaming a
  placement is not a wire change; reordering its properties is, and two peers built from different
  orders refuse each other rather than misreading.
- Every length is checked against what is left of the body before it is read. A truncated or hostile
  snapshot is counted in `ReplicationRefusals` and dropped.

## What the sequence does per cycle

1. Pump the link; arrivals come back through the attending reception.
2. Apply arrivals to placements this peer does **not** own, holding two samples of each.
3. Ease those placements toward the newer sample, a smoothing delay (0.1 s default) behind the
   newest arrival, so there is always a sample on each side of the moment being drawn.
4. Once the send interval is up (30 Hz default), gather the owned placements whose properties moved
   past their quantum and deliver one snapshot.

Authority is per placement, not per peer. A snapshot naming a placement this peer owns is refused as
`Unauthorised`, so two peers cannot fight over one. `Adopt()` moves authority and makes the new owner
send everything once, because the room has no way to know what the old owner had already told it.

A property's `Quantum` is the smallest change worth a packet. In the proof, twenty cycles of jitter
0.0001 wide under a 0.05 quantum send nothing at all, and one real change sends exactly one snapshot.

## Proof

`Exhibits/Workbench/Networking/ReplicationChecks.cpp`, run by
`Exhibits/Workbench/Networking/RunReplicationChecks.py` — **PASS 66**, then the same proof again under
AddressSanitizer and UndefinedBehaviorSanitizer. Half of what it asserts is that a hostile snapshot is
refused rather than read past, and a plain build cannot tell refusing apart from getting away with it;
the sanitized pass can. It replays the sound snapshot truncated at every length before its end.

No SDK, no socket, no window: two sequences are seated on two loopback links in one room.

## Build paths

The replication sources are in all three routes, as a build change must be:

- `Projects/Project-Networking/Build/ToolchainSequence.ps1` — the MSVC `/MD` x64 route
- `Projects/Project-Networking/Build/WindowHost/CMakeLists.txt` — the windowed diagnostic
- `Projects/Project-Networking/Build/ToolchainSequence.py` — the Linux route, which also builds the proof

## What CI holds to account without an SDK

`Exhibits/Workbench/Networking/RunSdkFreeChecks.py` — **PASS 80** across the transport, lobby and
login checks, and seven sources compiled under `-Wall -Wextra -Werror`. Only `EpicExchange`,
`LobbyRuntime`, `SessionHistory`, `LoginHost`, `WindowHost`, `EcomOwnership` and the real
`PhotonTransport` reach an SDK header; `RunChecks.py` covers those where one is installed.
