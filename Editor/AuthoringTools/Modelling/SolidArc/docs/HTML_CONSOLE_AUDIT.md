# SolidArc HTML prototype — audit of the C++ ↔ HTML seam, and the fix

**Scope: the HTML prototype only.** The C++ kernel is not touched. This document records what was
actually wrong, why the AI kept failing to "integrate SolidArc into HTML", and what was changed.

---

## 1. The real mistake

The panel is *not* short of geometry. `Panel/index.html` has a competent B-rep extruder, analytic
slots, rolling-ball fillets, a constraint solver, a Canvas-2D software raster and a 414-check smoke
suite that passes. The kernels are fine on both sides.

**The mistake is that the two halves speak different languages, and the HTML side hides the
disagreement instead of reporting it.**

`Console/CommandCodec.cpp` defines one grammar for SolidArc:

```
verb arg arg …      # comment
(x,y,z) (x,y)       vectors, whitespace allowed inside
--flag  --flag=v    switches
a ; b               several commands per line
"quoted name"       quoted text
```

`ConsoleHost.cpp` registers **64 verbs** against that grammar, and the 231 files of `Scripts/*.arc`
drive the whole engine through it.

The HTML prototype's command line was this:

```js
function runCmd(s){const [v,...a]=s.split(/\s+/);const N=a.map(Number);snapshot(s);log(s);
  if(v==='box'&&a.length>=3){…}
  else if(v==='cylinder'&&a.length>=2){…}
  …
  else log('unknown verb: '+v);
  rebuildAll();}
```

`split(/\s+/)` + `Number()`. **10 verbs.** No vectors, no flags, no `;`, no comments, no quotes.

### Measured consequences (before the fix)

Replaying real `.arc` lines through the panel:

| `.arc` line (valid against the C++ host) | Panel result |
|---|---|
| `box (0,0,0) 40 30 20` | **silent corruption** — `w:null, d:40, h:30`, body drawn wrong |
| `sphere (0,0,5) 8` | no-op |
| `circle (0,0) 12` | no-op |
| `rect (-6,-2) (-2,2)` | no-op |
| `polygon (5,3) 1.5 6` | no-op |
| `torus (0,0,2) 6 2` | no-op |
| `cone (0,0,0) 4 1 8` | no-op |
| `line (-8,-8) (8,-8)` | no-op |
| `list`, `help`, `echo`, `reset` | no-op |

`box (0,0,0) 40 30 20` was the worst case: `Number("(0,0,0)")` → `NaN` → `w:null`. The parser
accepted a line it did not understand and produced a wrong solid **with no error**. That is the
behaviour that reads as "the AI can't integrate CCAD into HTML" — the prototype looked like it was
ignoring or mangling the kernel's own scripts.

### Three defects behind it

1. **No shared grammar.** The panel had no tokeniser at all, so no `.arc` line with a vector or a
   flag could ever work. Every verb would have had to hand-roll its own parsing.
2. **No refusals.** The C++ host is fail-fast: `Refuse("…")` prints why and halts a script. The panel
   silently did nothing, or did the wrong thing. Failure was indistinguishable from success.
3. **Output went nowhere.** `log()` writes to `$('#log')`, but `id="log"` is emitted *only* inside
   `renderInspector()` when a single figure is selected. With an empty selection — the state you are
   in when you type a command — `log()` resolved to `null` and **every message was discarded**,
   including "unknown verb". The prototype was mute by construction.

Plus a fourth, smaller one: `snapshot(s)` ran *before* dispatch and was never rolled back, so typing
a typo pushed an undo entry that restored nothing. Undo history filled with no-ops.

---

## 2. What was changed

All changes are inside `Panel/index.html` (then synced to `docs/solidarc/` by `Scripts/publish-pages.sh`).

### 2.1 `arcDecode()` — a faithful JS port of `CommandCodec::Decode`
Same rules as the C++ tokeniser, ported statement for statement: `#` comments, `"quotes"`,
paren-depth tracking with whitespace allowed inside vectors, `;` command separation, and
`--flag[=value]` split into a flag list. `arcNumber` / `arcPoint` mirror `ParseNumber` /
`ParsePoint`, including "a number must consume the whole token" (so `40mm` is refused, not
silently read as `40`).

Malformed input now reports `unbalanced '('` / `unterminated string`, exactly like the host.

### 2.2 A command registry with real refusals
Verbs are entries in a `CMDS` table carrying a `usage` string copied from the C++ `Add(verb, help,…)`
registration, so the two help texts cannot drift. Every verb validates arity and argument *type* and
calls `refuse()` on mismatch:

```
arc › box 40 30 20
✗ box: argument 1 must be a point (x,y[,z]) — usage: box (cornerA) (cornerB) · box (corner) dx dy dz
```

`refuse()` returns false, which makes `runCmd` **drop the undo snapshot** — a refused command leaves
no trace in history. Pure queries (`list`, `help`, `echo`, `view`, `select`, `measure`, `topology`)
are marked `query:true` and never snapshot at all.

Verbs that exist in the C++ host but are genuinely not in the prototype yet (`loft`, `sweep`,
`boolean`, `fairpatch`, `array`, …) are registered as **known-but-unimplemented**. They refuse with
`not in the HTML prototype yet — C++ host only`, which is a different message from
`unknown verb '…'`. That distinction is the whole point: it tells you whether the panel is behind
the kernel or whether you mistyped.

### 2.3 `.arc` grammar now actually runs
Implemented against the panel's existing builders, using the same code paths the UI uses
(`commitShape`-style sketch registration, `solidBuild`, `invalidate`, `snapshot`), so the command
line cannot diverge from the mouse:

`echo · reset · help · list · select · view · move · hide · show · delete · undo · redo ·
plane · workplane · line · rect · circle · arc · ellipse · polygon · slot · polyline · point ·
box · cylinder · sphere · cone · torus · extrude · dim · measure · topology · fillet · chamfer`

Points are read as workplane (u,v) for sketch curves and as world (x,y,z) for solids, matching the
host. Flags are honoured where the panel supports them (`--name=`, `--construction`, `--center`,
`--radius=`, `--rotation=`, `--circumscribed`, `--offset=`, `--sheet` …).

### 2.4 Three new primitive bodies
`sphere`, `cone` and `torus` are registered in `.arc` scripts (27, 4 and 11 uses respectively) but
had **no mesh at all** in the panel — another reason scripts appeared to do nothing. Added
`sphereMesh` / `torusMesh` (cone reuses the existing tapered `cylMesh`), wired into `meshOf`,
`measure` (exact analytic volumes) and `boundsOf`.

### 2.5 The console is visible
Added a persistent `#conlog` pane directly above the command input in the Inspector. It is part of
the static markup, so it exists regardless of selection. `log()` writes there always, keeps 200
lines, auto-scrolls, and colours refusals red. The old selection-scoped `#log` still mirrors output
when a figure is selected.

Echoing the command back (`› box (0,0,0) 40 30 20`) before its result gives the same transcript shape
as the C++ REPL.

### 2.6 `view` completed
The host has 8 canonical views plus `fit`; the panel had 4. Added `back`, `left`, `bottom`, `persp`
and `view fit [selected]` (routed to the existing `focusSelection`).

---

## 3. Verification

`Verification/smoke.js` grew a section **§31 — .arc grammar parity** (the suite is now at 460 checks,
all passing). It asserts:

- the tokeniser handles vectors, whitespace in vectors, flags, `;`, `#`, quotes;
- `box (0,0,0) 40 30 20` builds a body with **finite** `w/d/h` (the original corruption);
- every no-op line in the table above now creates the figure it names;
- a refused command creates no figure **and no undo entry**;
- unknown vs. not-yet-implemented produce different messages;
- `sphere` / `cone` / `torus` build meshes with positive volume;
- a representative slice of `Scripts/Phase2_Primitives.arc` replays end to end.

```
$ node Verification/smoke.js
smoke: 460 checks OK · 41 figures
```

---

## 4. What this unblocks for the C++ side

The seam is now a **grammar**, not a pile of `if(v==='…')`. When the console host is bridged, the
panel's `runCmd` becomes a transport call and nothing above it changes:

```js
function runCmd(s){ send(s); }            // ConsoleHost::Execute(s)
// refusals already render; the transcript already looks like the REPL
```

Because both sides now tokenise identically and share usage strings, a `.arc` script is a genuine
conformance test: run it against the panel and against `SolidArcConsole`, and any behavioural
difference is a real kernel difference rather than a parsing artefact.

**Remaining prototype gaps** (deliberately refusing, listed so the C++ phase knows the target):
`loft`, `sweep`, `pipe`, `revolve`, `boolean`, `fairpatch`, `fillpatch`, `patch`, `sew`, `solidify`,
`array`, `radial`, `bridge`, `join`, `explode`, `trim`, `offset`, `mirror`, `constraint`, `spline`,
`cpcurve`, `render`, `pick`, `matcap`, `tint`, `gizmo`, `recipe`, `describe`, `dependents`,
`intersections`, `areas`, `profile`, `empty`, `rename`, `fill`, `angle`.
