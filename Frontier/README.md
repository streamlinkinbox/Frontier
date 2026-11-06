# Frontier engine — session & game mechanics

The Session/game-mechanics slice, redesigned and verified standalone. It has **no engine, renderer or
GLFW dependency**: everything builds with any C++17 compiler, and `Validation/SessionValidation.cpp`
exercises the session and Everest navigation contract (78 checks) without a window.

```sh
g++ -std=c++17 -I. Validation/SessionValidation.cpp Frontier/Engine/Input/*.cpp \
    Frontier/Engine/Scene/*.cpp Frontier/Engine/Session/*.cpp \
    Projects/Everest/Source/Session/*.cpp Projects/Everest/Source/Navigation/*.cpp \
    -o /tmp/SessionValidation && /tmp/SessionValidation
# or: cmake -S Frontier -B /tmp/frontier-build && cmake --build /tmp/frontier-build
/tmp/frontier-build/Everest --render /tmp/lobby.ppm
/tmp/frontier-build/Everest --navigate proving-grounds --render /tmp/proving-grounds.ppm
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

## The game (C++ Lobby)

`Frontier/Engine/Session/LobbySession` is the game at this step. It opens on the **Lobby by default**,
builds its room by reading the assigned policy (`LobbyPolicy`: at most **5 players**, **no
respawn**, **no derez**, parked spawn), and stands **one vehicle** in the studio — the same
ControlVehicle, from the same `FCT1` facet archives as the HTML renderer
(`Engine/Scene/FacetArchive`), with a steering wheel that follows the steer axis and a
**tablet that is powered but empty** (no HMI yet). Other policies (ProvingGrounds, Race,
Elimination, Everest's) plug in through the same creation path. The generic `LobbySession` sample also
retains a debug popup (`Ctrl+Shift+L`, then a digit; `FRONTIER_DEBUG_LEVEL_SWITCH=0` compiles it out).
The Project Everest executable instead uses its C++ Navigation app to route normally between the Lobby
and Proving Grounds in both directions, preserving profiles and deploying their vehicles under the new
level policy.

```sh
g++ -std=c++17 -I. Frontier/Game/LobbySessionMain.cpp Frontier/Engine/Input/*.cpp \
    Frontier/Engine/Scene/*.cpp Frontier/Engine/Session/*.cpp Frontier/Game/*.cpp \
    Projects/Everest/Source/Session/*.cpp -o /tmp/LobbySession
/tmp/LobbySession --guests 4 --render /tmp/lobby.ppm   # CPU presentation: Game/SoftwarePresentation
```

`VisualProof/LobbyPreview.png` is that render: five cars in the circle, the softbox overhead, the
hero's cyan wheel light — the C++ counterpart of the prototype's Lobby, before any GPU
front-end. A GLFW window (with `FRONTIER_WITH_GLFW`) and the WebGPU presentation replace
`SoftwarePresentation` without touching the session, policy or vehicle code.

## Where things live

- **Visual effects** (anything that changes how a vehicle is *looked at*, never how it behaves):
  `Frontier/Engine/Effects/` — first resident is `VehicleInspection` (Solid / Ghost / Wireframe /
  **X-ray Scan**, the travelling band; `VisualProof/XRayScanPreview.png`, render with
  `LobbySession --xray`). Presentations read it; physics and the session never do.
- **Session and its policies**: `Frontier/Engine/Session/` — including `LobbySession`, the game
  session that opens on the Lobby. (There is deliberately no `LobbyGame`.)
- **Render proof** belongs in `VisualProof/` at the repository root, as in the editor exhibits —
  not beside the code that produced it.
