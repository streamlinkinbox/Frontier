# Frontier engine — session & game mechanics

The Session/game-mechanics slice, redesigned and verified standalone. It has **no engine, renderer or
GLFW dependency**: everything builds with any C++17 compiler, and `Validation/SessionValidation.cpp`
exercises the whole contract (42 checks) without a window.

```sh
g++ -std=c++17 -I. Validation/SessionValidation.cpp Frontier/Engine/Input/*.cpp \
    Frontier/Engine/Scene/*.cpp Frontier/Engine/Session/*.cpp \
    Projects/Everest/Source/Session/*.cpp -o /tmp/SessionValidation && /tmp/SessionValidation
# or: cmake -S Frontier -B /tmp/frontier-build && cmake --build /tmp/frontier-build
```

## How we got here (audit of `SultanAladin/Frontier` @ `bd738ed`)

The other agent's session work was read file by file before any of it was kept. What was sound: the
`InputPacket` contract, the encoder's pedal/steer ramps, the footprint-survey spawn stand, the policy
family (Lobby/ProvingGrounds/Race/Elimination) and Everest keeping its own overrides in the project.
What was not kept, and why:

- **It cannot build outside their tree.** Every class is welded to `WorldSpace`/`RecordToken`/
  `PlayerStore`/`PhysicsSpace`, none of which exist here — so none of it was ever verifiable on its own.
  This redesign depends on nothing outside `Frontier/Engine` + `Projects/Everest`.
- **`DeployableUnit` was an alias** (`using DeployableUnit = Agent;`). Here it is the actual pawn class,
  as specified, and `Agent` is what the name should mean: the control stand between a driver and a unit.
- **One class did five jobs.** Their `Agent` was pawn, appearance, spawn stand, brake rules and world
  record at once. Here: `DeployableUnit` (pawn + deployment), `Agent` (possession), drivers (packets),
  policies (rules).
- **The Everest `RaceProcedure`/`EliminationProcedure` files from the brief do not exist on that
  branch** — only a `SessionPolicy` pair does. Game rules here are policies (`RacePolicy`,
  `EliminationPolicy`), never procedures, per the naming rules.

## The contract

- **Input is `InputPacket`, full stop.** Devices encode straight into packets (`InputEncoder`; GLFW
  polling compiles in with `FRONTIER_WITH_GLFW`). There is no action-map layer.
- **`InputDriver` drives an Agent (human); `SyntheticDriver` drives an Agent (AI)** — identical
  packets, so possession can swap without the unit noticing.
- **`DeployableUnit` deploys itself**: `Deploy()` stands it under the session's `SpawnPolicy`
  (footprint survey — the same stand on a pad, a kerb, a bank, a cross-slope), including brake rules
  at the start (enabled, disabled for a hold, or parked on the handbrake: `StudioParked`,
  `GridRelease`, `SuspendedDrop`).
- **Rules are policies in `Session/SessionPolicy.h`**: `NetPolicy`, `RespawnPolicy` (never in the
  Lobby; grid or last-checkpoint elsewhere) and the game rules (`RacePolicy`, `EliminationPolicy`).
  Engine defaults live in the engine; **Everest's own numbers live in
  `Projects/Everest/Source/Session`** and reach the session through its factory.
- **`SessionSpace`** is the room: players join/leave, deploy at studio slots (host centre, guests on
  the ring — the Lobby arrangement), ready up, and `LevelTransition` carries their profiles
  (nametag + Rank — the same pair the Lobby's number plates wear) into the next level's policy.
