// Compiles every WGSL module of the fluid library and reports compiler diagnostics.
import { getDevice } from './gpu.mjs';
import * as SIM from '../src/shaders/sim.wgsl.js';
import * as PART from '../src/shaders/particles.wgsl.js';
import * as REND from '../src/shaders/render.wgsl.js';

const { device } = await getDevice({ verbose: false });
const modules = [];
for (const [k, v] of Object.entries(SIM)) if (typeof v === 'string') modules.push(['sim.' + k, v]);
for (const [k, v] of Object.entries(PART)) if (typeof v === 'string') modules.push(['particles.' + k, v]);
for (const [k, v] of Object.entries(REND)) if (typeof v === 'string' && /@(vertex|fragment|compute)/.test(v)) modules.push(['render.' + k, v]);
// extra modules registered by other files (demo shaders) can be validated by passing paths
for (const extra of process.argv.slice(2)) {
  const m = await import(new URL(extra, `file://${process.cwd()}/`).href);
  for (const [k, v] of Object.entries(m)) if (typeof v === 'string' && /@(vertex|fragment|compute)/.test(v)) modules.push([extra + '.' + k, v]);
}

let bad = 0;
for (const [name, code] of modules) {
  device.pushErrorScope('validation');
  const mod = device.createShaderModule({ code, label: name });
  const info = await mod.getCompilationInfo();
  const err = await device.popErrorScope();
  const msgs = info.messages.filter((m) => m.type !== 'info');
  if (msgs.length || err) {
    if (msgs.some((m) => m.type === 'error') || err) bad++;
    console.log(`--- ${name}`);
    const lines = code.split('\n');
    for (const m of msgs) {
      console.log(`  ${m.type} L${m.lineNum}:${m.linePos} ${m.message}`);
      if (m.lineNum) console.log('    > ' + (lines[m.lineNum - 1] ?? '').trim());
    }
    if (err && !msgs.length) console.log('  ' + err.message);
  } else {
    console.log(`ok  ${name}`);
  }
}
console.log(bad ? `\n${bad} module(s) failed` : '\nall modules compiled');
process.exit(bad ? 1 : 0);
