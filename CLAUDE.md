# CLAUDE.md — standing instructions for AI work in this repo

## Who runs what (the one rule — never violate, never forget)

- The AI sandbox has NO GPU and no Vulkan SDK. The AI EXECUTES CPU-only, always.
- The AI's job is to SIMULATE on CPU exactly what the user's GPU build computes:
  same scene, same math, same pixels. Proof images, hashes, and parity checks
  exist so the CPU side predicts the user's Vulkan side.
- The AI WRITES code for both builds (including GPU-side wiring it cannot run
  and must verify by review + signature checks instead), but RUNS CPU-only.
- Never talk as if doing Vulkan work. Describe the user's build only as
  "what you compile and run"; describe the AI's work as the CPU simulation.

## Repository layout (never add top-level folders without asking)

- Eight top-level folders, no more: `Engine/`, `EngineContent/`,
  `ExternalPackages/` (git submodules — never `git add` their contents;
  they seat via `git submodule update --init`), `Projects/`, `Tools/`
  (`Build/` for build-time steps incl. the imgui patch stack,
  `Scripts/` for dev utilities), `Docs/`, `Exhibits/`, `Experimental/`
  (HTML-first prototypes: `Ocean/`, `Liquid/`, `Water/`).
- The verification wing is `Exhibits/`: `Exhibits/Workbench/<topic>/`
  holds runnable proof harnesses, `Exhibits/Gallery/<topic>/` holds the
  kept proof sheets. New topics (Sun, Sky, LensFlare, …) follow the same
  pair. The live gate proof is
  `Exhibits/Workbench/Editor/CheckEditorProof.sh` — run it green before
  every commit that touches the editor, the shade, or the patch stack.
- Naming conventions: verification artifacts are Proofs, Gates, Sheets,
  Exhibits — never "tests", "specs", or "suites". No-SDK stand-ins are
  Counterparts (never "shims", "mocks", or "stubs"). The project is
  Frontier; the material domain's `slate_*` (glTF extras terms) is a
  different slate and stays untouched.
- Banned words, in code, docs, comments and commit messages alike:
  **"kind"** (and "kinds") — it is vague about a thing that is always
  nameable more precisely. Say what the thing actually is: a file type, a
  table, a file, a value, a record, a member of the family, a variant. A
  table or field that would have been `KIND`/`Kind` is `TYPE`/`Type` (or a
  precise name like `MaterialSlot`), and a sentence that would have said
  "a different kind of X" names the X. "Type" is the sanctioned stand-in;
  a name that says more is better still. Existing uses predate the ban
  (`MaterialCodec.cpp`'s JSON parser field, a few comments) and are swept
  when those files are next touched — never introduce a new one.
- `Scratchpad/` is permanent transient space: experiments live there
  untracked while in use; graduates move to `Exhibits/Workbench/` or
  `Exhibits/Gallery/`. Never commit stale one-off proofs or byproduct
  images anywhere else.
