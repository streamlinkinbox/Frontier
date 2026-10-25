// Validates every WGSL shader with the wgsl_reflect parser.
// Usage: npm run validate-wgsl
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { WgslReflect } from 'wgsl_reflect/wgsl_reflect.module.js';

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
    try {
      const reflect = new WgslReflect(src);
      const eps = [
        ...reflect.entry.compute.map((e) => `compute:${e.name}`),
        ...reflect.entry.vertex.map((e) => `vertex:${e.name}`),
        ...reflect.entry.fragment.map((e) => `fragment:${e.name}`),
      ];
      console.log(`  OK  ${path}  [${eps.join(', ')}]`);
    } catch (err) {
      failed++;
      console.error(`FAIL  ${path}\n      ${String(err.message ?? err).split('\n')[0]}`);
    }
  }
}
console.log(`\n${checked - failed}/${checked} shaders valid`);
process.exit(failed ? 1 : 0);
