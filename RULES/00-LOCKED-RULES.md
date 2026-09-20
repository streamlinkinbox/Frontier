# 🔒 LOCKED RULES — canonical C++ rules for every project

**Status: USER-OWNED. Agents read, never write.** How the lock works and how to install this kit
in a new project: `RULES/PROTECTION.md`. To change a rule, tell the user — only they edit this file.

Source of truth: [SultanAladin/Slate](https://github.com/SultanAladin/Slate) — the root `CLAUDE.md`
(§ C++ & Architecture Rules) and `AgenticInstuctions/SKILL-Naming.md`, which is copied **verbatim**
next to this file as `RULES/SKILL-Naming.md`. Items marked **(Slate)** are Slate-engine specifics;
everything else is portable to every project.

---

## Rule 1 — C++ standard: C++20, always

- Compile as **C++20** in every configuration and every target: `/std:c++20` (MSVC) or
  `-std=c++20` (GCC/Clang). Never downgrade; never use an older standard "just for this file".
- Prefer `constexpr` and compile-time checks over runtime validation wherever a gate can be
  expressed that way.
- **(Slate)** `/MD` in every configuration. Debug code is selected by `SLATE_DEBUG`; `_DEBUG` is never used.
- **(Slate)** No exceptions across a unit seam. No `new`/`delete` outside an extent slicer.
- **(Slate)** Absence carries a reason: return `Deliver<T>` with a `Refusal`, not `std::optional`,
  wherever something can be rejected or reported. A convergent computation returns
  `ConvergentResult<T>`, never a bare value. Identities are `Identity<Subject>` with distinct
  tags — a `PartitionIdentity` must not be passable where an `OwnerIdentity` is expected.
- **(Slate)** Every exported computation carries `SLATE_DECLARES_PRECISION(...)` naming what it
  claims and what it consumes; the transitivity rule is a `static_assert`, not a review item.
- **(Slate)** Vendor spellings are verbatim: `VkBuffer`, `VkPipeline`, `ImDrawData`.

## Rule 2 — Use templated functions

- **A shared algorithm is written ONCE, as a template.** Per-subject code is the differences;
  the walk is written once. (This is Slate's own stated rationale for templating its
  tessellation walk and intersection dispatch over the surface tag.)
- Never fork a per-type copy of an algorithm a template already covers. If two functions differ
  only by a type, they are one `template <typename T>` function — not two.
- The engine's core vocabulary is itself templated; follow the same shape:
  `Deliver<Content>`, `Identity<Subject>`, `ConvergentResult<T>`, and integrators/projections
  templated over `Integrand` / `Sampler` / `Spectrum`.
- C++20 is available: constrain template parameters with concepts / `requires` rather than the
  old SFINAE style, and prefer `constexpr` machinery the standard makes possible.

## Rule 3 — Naming convention

`RULES/SKILL-Naming.md` is the **full and final authority**. The load-bearing essentials:

- Never recall a name from memory. Construct every name from the physical or mathematical
  mechanism it implements, in one plain sentence first, then fuse the technical verb and the
  target noun.
- Modules and folders: PascalCase `<Subject><Role>` using the **closed 19-entry role suffix list**
  and nothing else — `Sequence`, `Codec`, `Exchange`, `Interchange`, `Extension`, `Solver`,
  `Integrator`, `Classifier`, `Projection`, `Specification`, `Structure`, `Space`, `Index`,
  `Metrics`, `Scheduler`, `Queue`, `Panel`, `Host`, `Depot`.
- **Retired role suffixes — never use:** `Boundary`, `Region`, `Tree` (replaced by
  `Exchange` / `Interchange` / `Extension` for edges; `Space` for extents; `Structure` for topology).
- ALL internal identifiers (folders, files, classes, variables, functions) use PascalCase.
- Zero shorthand — full words only: `Parameter` not `Param`, `Specification` not `Spec`,
  `Information` not `Info`, `Windowing` not `Wsi`. Zero single-letter names. Zero `k` constant
  prefixes — use `MinimumBoundary`, `ToleranceThreshold`.
- A variable never mirrors its class type's name: forbidden `TopologyCluster TopologyCluster`;
  required `ActiveTopologyCluster` or `PrimaryTopologyCluster`.
- Booleans are never prefixed `is` / `has` / `can` — state the property directly as a noun phrase:
  `BoundaryCondition`, `ClosedEnabled`, `VisibilityEnabled`.
- Functions: PascalCase domain-specific technical verbs only — Solve, Integrate, Traverse,
  Construct, Resolve, Reclaim, Linearize, Classify, Project. **Banned verbs:** Get, Set, Process,
  Handle, Manage, Commit, Compose, Draw, Update, Evaluate.
- Banned words (full lists in `SKILL-Naming.md`): categorical spellings such as `Kind` (also in
  prose), `Type`, `Sort`, `Variety`, `Flavour`, `Category`, `Class` as a noun; family terms
  (Parent, Child, Sibling, Sister, Neighbor, Ancestor, Descendant, Orphan); and the structural
  list (Handle, Store, Cache, Registry, Pool, Table, Map, Buffer, Pipeline, Manager-style
  categories, …). When in doubt, check the file — the substitution tables give the replacement.
- Third-party API exemption: direct Vulkan / SDK calls mirror the vendor API verbatim (e.g.
  `VkDeviceCreateInfo`). All internal code enforces the full rules.
- Mathematical vocabulary exemption is narrow: a term is allowed where it names a *defined
  mathematical object* (`Graph`, `Predicate`, `Quadrature`, `Kernel`, …), never as a software
  category. See `SKILL-Naming.md` for the exact gate.

---

## Change protocol

1. Only the **user** edits `CLAUDE.md` or anything in `RULES/` (their editor works normally;
   they commit with `git commit --no-verify`).
2. An agent that believes a rule is wrong, missing, or blocking correct work **stops and tells
   the user**, quoting the rule. It does not edit, and does not invent a project-local exception.
3. Any legitimate change is committed by the user with a message starting `rules-update:` so the
   history of the rules stays auditable.
