# SKILL-SymbolIndex — read the index, not the file

A `.symbolindex` is a generated digest of one source folder: every symbol as one row — what it does,
which file, which line span, parameters with units, return, cost, callers. Authoring `///` blocks and
the full format spec live in `DOC/SymbolIndex.md`; you need neither in order to read an index.

- Folder has an index → read the index, never the sources.
- Need the body anyway → read **only** the `first-last` span. Never widen for context.
- `?` = undocumented, a work item. `-` = known empty. `|` separates columns, never inside a field.
- Never hand-edit an index — it is regenerated. `check` says stale → rebuild before trusting it.

Location unknown — `python Tools/SymbolIndex.py find <regex>`. Folder known — read
`<Folder>/<Folder>.symbolindex`. One symbol in full — `… read <Type::Name>`. Body genuinely needed —
`Read <file> offset <first> limit <last − first + 1>`.

```
F Normalize | VectorProjection.cpp | 118-143 | api,pure,simd | ✔️ | Scales 𝑣 to unit length; …
    in   𝑣  const Vector3&  [-]  direction of any magnitude, may be zero
    out  -  Vector3         [-]  unit vector, ‖r‖ = 1 ± ε
    err  returns (0,0,0) when ‖𝑣‖ < ε; never throws
    by   SurfaceProjection/NormalSolver.cpp
```

`kind name | file | first-last | tags | cost | purpose`, details indented four spaces. Kinds — `F`
function · `T` type · `E` enum · `V` constant · `A` alias · `K` macro · `X` interop · `S` source file ·
`I` child index. `%` lines are directives, `//` lines are banners. Cost rises `✔️` → `🚩` → `🔴`.

The index already answers: signature · units · return · allocates · throws · callers · which file · a
folder's public surface · whether a name is taken. Never open a source file for any of those.
