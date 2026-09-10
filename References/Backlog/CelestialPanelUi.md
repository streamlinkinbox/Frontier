══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  The Celestial panel UI — scheduled last, after every component ships
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

**Status: deferred by decision, not by difficulty. Build it once every simulation component exists.**

The step plan originally put the panel at step 2, on the reasoning that early UI gives each later entity a proven
frame to plug into. That reasoning was wrong in one important way, and the decision to move it is the user's:
**build the things the panel controls first, then build the panel once, against a complete and settled parameter
set.**

Why that is the better order:

  • The panel is a *projection* of the entity registry. Every component added while the panel exists forces a
    second edit — the component, then its inspector — and every parameter that gets renamed or removed during
    implementation is a widget rebuilt for nothing. Sixteen entities is sixteen chances to pay that twice.
  • The bespoke widgets are the expensive part (≈60% of total port effort by the plan's own estimate), and they
    are only cheap to build once the thing they manipulate is stable. `ORB` drags azimuth and time together;
    `WINDROSE` drags bearing against a Beaufort readout; `DISC` drags angular diameter on the ring and softness
    inside it. Each is a direct manipulation of a live simulation value, and none can be finished against a
    parameter that is still moving.
  • Nothing is blocked by its absence. Each component is provable headless with committed sheets and numeric
    gates, which is how steps 0 and 1 were proved and is stronger evidence than a screenshot of a slider.

**This is a scheduling decision, not a scope cut.** The panel is still the deliverable that makes the system
usable, the design is still the demo's, and the proof mechanism is still the one repaired in step 0.


WHAT IS ALREADY IN PLACE FOR IT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Work already done that the panel will consume when its turn comes:

  • **The proof mechanism, repaired and verified.** `Scratchpad/CheckEditorProof.sh` drives `EditorHost` through
    the engine's real tick order, rasterises the ImGui draw lists on the CPU with no Vulkan and no window, writes
    sheets to `Diagnostics/`, and `EditorKnobCheck` reads them back and asserts geometry numerically (it reports,
    for example, `knobs found: 1 · middle 1214,341 size 23x24 · the knobs sit on their fractions`). Step 0 fixed
    its silent failure on a fresh checkout, where a missing `ExternalPackages/stb` produced a raw compiler error
    instead of an actionable message.
  • **The tier ladder**, in `FidelityClassifier` and nowhere else, guarded by `CheckCelestialTiers.sh` against a
    second copy drifting. The panel must *read* these values, never restate them.
  • **The design tokens**, transcribed from the demo and recorded in `CelestialPortPlan.md` §4:
    `--glass rgba(15,16,18,.74)`, `--g2/.045 --g3/.08`, `--stroke .07 / --stroke2 .13`,
    `--text .94 / --t2 .56 / --t3 .32`, `--orange #ffb454`, `--green #34c759`, `--red #ff3b30`,
    `--ease cubic-bezier(.22,.61,.36,1)`, `--spring cubic-bezier(.175,.885,.32,1.15)`, Outfit / JetBrains Mono,
    28 px panel radius, 316 px outliner.
  • **The registry shape**: 19 entities (16 in scope, lights excluded, Cine Camera deferred) each declaring
    `name / kind / color / icon / preview / hero` plus sections of typed properties, and 21 widget types —
    `COL CURVE DISC DROP GAUGE GLOBE HIST HORIZON KELVIN LENS LINKNOTE METER ORB ORBIT2 PAD PLANET SEG STEP TOG
    TRK WINDROSE`.


THE ONE THING TO WATCH WHILE BUILDING COMPONENTS WITHOUT IT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Without a panel forcing the question, each component can quietly invent its own settings struct, and the panel
then has to reconcile sixteen inconsistent shapes at the end. Keep every component's parameters in one settings
struct per entity, named as the demo names them, with units in the comment — the way
`VisibilityRaster::CelestialSettings` and `TwilightSettings` already are. That costs nothing now and is the
difference between the panel being a projection and being a rewrite.
