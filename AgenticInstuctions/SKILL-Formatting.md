# SKILL-Formatting — Visual Formatting, Annotation & Emoji Rules

Binding on all C, C++, shader and engine-Markdown output. Structural clarity and visual consistency
come first. Check output against this file before returning it. Naming is governed separately by
`SKILL-Naming.md`; the `.symbolindex` digest by `SKILL-SymbolIndex.md`.

---

## 1. File headers — required on every source file

- Ruler above and below the title: `//===…`, **exactly 142 characters**.
- Title line: filename in ALL CAPS, strictly centred between the rulers.
- Immediately below: `// 📦 [single-line module description]`, then exactly 1 blank line.

```cpp
//============================================================================================================================================
//                                                          BOUNDARYSTRUCTURE.H
//============================================================================================================================================
// 📦 Radial-edge boundary representation — generational tokens over vertex, edge, loop and face records.
```

## 2. Section banners

- Ruler above and below: `//---…`, **exactly 122 characters** (never below 80).
- Title in ALL CAPS, strictly centred. Exactly 1 blank line before the banner and after it.
- Banner titles are carried verbatim into the generated index, so they are the reader's shared
  skeleton across source and digest. Name the *responsibility*, not the language construct.

```cpp
//------------------------------------------------------------------------------------------------------------------------
//                                                     TOPOLOGY QUERIES
//------------------------------------------------------------------------------------------------------------------------
```

## 3. Comments

- Inline comments are vertically aligned as a column. Format — `Type Name;   // [unit] - description`.
- The unit is always present and always bracketed: `[mm]`, `[ms]`, `[deg]`, `[-]` for dimensionless.
- Standalone implementation notes above the code they explain: `// 📝 …`.
- Ordered steps use `①–⑩`, one step per line, aligned.
- No commented-out code. Delete it; the history keeps it.

```cpp
float    EdgeTolerance = 1.0e-5f;   // [mm]  - below this two vertices weld
uint32_t RadialDegree  = 0u;        // [-]   - faces incident to this edge
```

## 4. Annotation blocks — `///` above a declaration

The machine-read documentation. Placed immediately above the declaration with no blank line between.
Keys are lowercase ASCII in fixed order — prose, `in`, `out`, `err`, `use`, `cost`, `pre`, `post`,
`note`, `tag` — values aligned into a column. Full key semantics live in `SKILL-SymbolIndex.md`.

```cpp
/// 📦 Walks the radial ring of ActiveEdge and returns every face incident to it.
/// in    ActiveEdge   [-]  edge token; a stale generation yields an empty result
/// out   FaceSpan     [-]  faces in radial order, empty when the edge is vacant
/// err   returns an empty span for a stale or vacant token; never throws
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
FaceSpan IncidentFaces(BoundaryToken<BoundaryTag::Edge> ActiveEdge) const;
```

Never hand-write line spans, filenames, hashes, callers or counts into a comment — all of that is
derived by the indexer and a hand-written copy will silently rot.

## 5. Mathematical and physical variables

- MANDATORY: real Greek and Unicode glyphs — β, γ, Δτ, 𝑣, 𝑐, θ, ω, ε, ‖·‖ — never ASCII
  transliterations (`beta`, `dTau`, `epsilon`).
- This is the **only** sanctioned exception to PascalCase identifiers.

## 6. Alignment

- Align `=` vertically within a logical block of assignments.
- Align struct members into columns: type, name, comment.
- 4 or more parameters — one per line, types and names aligned.
- Align the `|` columns of every Markdown table.

## 7. Braces, spacing, indentation

- Allman braces — opening brace on its own line. A single-line conditional may omit braces.
- 4 spaces per level. **STRICTLY NO TABS.**
- Namespace contents stay **FLAT** — never indent inside a namespace.
- 1 blank line between functions, 2 between major groups, at most 1 inside a function body.
- No trailing whitespace. Files end with exactly one newline.

## 8. Markdown

- `#` is reserved for the file title. All other headings use `##` / `###`.
- Maximum line width 120 characters. Bullets use `-`. Tables are column-aligned.
- Fenced blocks always carry a language tag (`cpp`, `powershell`, `python`, or none for plain output).

---

## 9. Emoji — closed whitelist

Only the emoji below may appear anywhere: source, comments, Markdown, chat, commit messages,
filenames. Never invent one, never borrow one from outside the table.

📦 is the one module marker (project directive) — it is used for the file-header banner and the first `///`
annotation line. The former puzzle-piece marker is retired: it appears nowhere in the tree and must never be reintroduced.

| Category      | Emoji            | Means                                | Where it may appear                     |
|---------------|------------------|--------------------------------------|-----------------------------------------|
| Module        | 📦               | File-header banner / module description | File header line; first `///` line     |
| Notes         | 📝               | Implementation note                  | Comment above code                       |
| Notes         | 💡               | Insight, non-obvious reasoning       | Comment, Markdown                        |
| Notes         | ⚠️               | Warning — correct but easy to misuse | Comment, Markdown                        |
| Notes         | 🔴               | Critical                             | Comment, Markdown                        |
| Notes         | 🐞 / 🐛          | Known bug                            | Comment, Markdown                        |
| Notes         | 🚧               | Work in progress                     | Comment, Markdown                        |
| Notes         | 🔍               | Debug-only path                      | Comment, Markdown                        |
| Symbol kind   | ⚙️               | Function / routine                   | Markdown and banners only, never inline  |
| Symbol kind   | 🧱               | Type — class, struct, union          | Markdown and banners only, never inline  |
| Symbol kind   | 🔢               | Constant or enumeration              | Markdown and banners only, never inline  |
| Symbol kind   | 🔗               | Interop edge, vendor boundary        | Markdown and banners only, never inline  |
| Concern       | 📐               | Mathematical derivation              | Comment, Markdown                        |
| Concern       | ⏱️               | Timing / performance concern         | Comment, Markdown                        |
| Concern       | 💾               | Memory or allocation concern         | Comment, Markdown                        |
| Concern       | 🧵               | Threading / concurrency concern      | Comment, Markdown                        |
| Status        | 🟢 / 🔴          | Pass·on·good / fail·off·bad          | Tables, status lines                     |
| Cost scale    | ✔️ / 🚩 / 🔴     | Low / medium / high                  | Decision tables, `/// cost`              |
| Ranking       | ⭐               | Plain star only                      | Tables, Markdown                         |
| Ranking       | 🏆 🥇 🥈 🥉 🏅 🎖️ | Rank                                 | Tables, Markdown                         |
| Tags & flags  | 🏷️               | Label                                | Tables, Markdown                         |
| Tags & flags  | 🚩               | Priority / breaking change           | Tables, Markdown                         |
| Milestones    | 🏳️ 🏴 🏁 🎌      | Milestone / status                   | Tables, Markdown                         |

- Status dots: 🟢 and 🔴 only. No other coloured dot exists.
- Cost and decision tables must state the scale direction in the legend —
  `✔️ low · 🚩 medium · 🔴 high (cost rises left to right)`.
- **Emoji are never parse keys.** `✔` (U+2714) and `✔️` (U+2714 U+FE0F) render identically and compare
  unequal, so a key spelled in emoji fails silently. Machine keys are lowercase ASCII; emoji are
  decoration and are normalised to the U+FE0F form on write.
- ⚠️ Emoji occupy two display columns. When aligning a column that contains one, measure display width,
  not character count.
- EXPLICITLY BANNED: 🌟, ✨, and every emoji absent from the table above.
