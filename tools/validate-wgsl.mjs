// Validates every WGSL shader with the REAL Naga compiler (wgpu's shader
// frontend, compiled to WASM) — full semantic validation, not just syntax.
// This catches type errors, duplicate builtins, bad bindings, etc. — the
// same class of errors a browser would reject at pipeline creation.
// Usage: npm run validate-wgsl
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { parseWgsl, validate } from 'naga-wasm';

const shaderDirs = ['src/fluid/shaders', 'src/engine/shaders'];
const commonPath = 'src/fluid/shaders/common.wgsl';
const framePath = 'src/engine/shaders/frame.wgsl';
const common = readFileSync(commonPath, 'utf8');
const frame = readFileSync(framePath, 'utf8');

let failed = 0;
let checked = 0;

for (const dir of shaderDirs) {
  let files = [];
  try { files = readdirSync(dir).filter((f) => f.endsWith('.wgsl')); } catch { continue; }
  for (const f of files.sort()) {
    const path = join(dir, f);
    if (path === commonPath || path === framePath) continue;
    let src = readFileSync(path, 'utf8');
    if (src.includes('//@@COMMON@@')) src = src.replace('//@@COMMON@@', common);
    if (src.includes('//@@FRAME@@')) src = src.replace('//@@FRAME@@', frame);
    checked++;
    let mod = null;
    let info = null;
    try {
      mod = parseWgsl(src);
      info = validate(mod);
      console.log(`  OK  ${path}`);
    } catch (err) {
      failed++;
      const msg = err.formatted ?? err.message ?? String(err);
      console.error(`FAIL  ${path}\n${msg.split('\n').map((l) => '      ' + l).join('\n')}\n`);
    } finally {
      info?.free?.();
      mod?.free?.();
    }
  }
}
console.log(`\n${checked - failed}/${checked} shaders valid (Naga semantic validation)`);
process.exit(failed ? 1 : 0);
