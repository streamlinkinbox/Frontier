// Validate every WGSL module with naga (wgpu's shader compiler) so shader errors are caught without a GPU.
import { parseWgsl, validate } from "naga-wasm";
import { ALL } from "../RoadGenerator/src/gpu/shaders.js";

let failed = 0;
for (const [name, src] of Object.entries(ALL)) {
  try {
    const m = parseWgsl(src);
    validate(m);
    console.log(`✓ ${name}`);
  } catch (e) {
    failed++;
    console.log(`✗ ${name}\n${String(e.message || e).split("\n").slice(0, 30).join("\n")}`);
  }
}
if (failed) process.exit(1);
