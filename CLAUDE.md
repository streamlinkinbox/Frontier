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
