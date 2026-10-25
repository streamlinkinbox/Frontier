// Runs a test script with a working WebGPU (Dawn) backend.
// On machines without a GPU / Vulkan driver, SwiftShader (CPU Vulkan) is extracted from the
// @sparticuz/chromium package and used instead, so tests also run on headless CI boxes.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';

const script = process.argv[2];
if (!script) { console.error('usage: node test/launch.mjs <test.mjs> [args]'); process.exit(2); }

function hasSystemVulkan() {
  if (process.env.VK_ICD_FILENAMES || process.env.FLUID_FORCE_SYSTEM_GPU) return true;
  for (const d of ['/usr/share/vulkan/icd.d', '/etc/vulkan/icd.d']) {
    try { if (fs.readdirSync(d).some((f) => f.endsWith('.json'))) return true; } catch { /* none */ }
  }
  return process.platform !== 'linux';
}

function extractTar(buf, dest) {
  let off = 0;
  while (off + 512 <= buf.length) {
    const name = buf.toString('utf8', off, off + 100).replace(/\0.*$/, '');
    if (!name) break;
    const size = parseInt(buf.toString('utf8', off + 124, off + 136).replace(/\0.*$/, '').trim() || '0', 8);
    const type = buf[off + 156];
    off += 512;
    if (type === 48 || type === 0) {
      const out = path.join(dest, path.basename(name));
      fs.writeFileSync(out, buf.subarray(off, off + size), { mode: 0o755 });
    }
    off += Math.ceil(size / 512) * 512;
  }
}

const env = { ...process.env };
if (!hasSystemVulkan()) {
  const dir = path.join(os.tmpdir(), 'frontier-swiftshader');
  const icd = path.join(dir, 'vk_swiftshader_icd.json');
  if (!fs.existsSync(icd)) {
    let pkg = null;
    for (let d = path.dirname(new URL(import.meta.url).pathname); d !== path.dirname(d); d = path.dirname(d)) {
      const cand = path.join(d, 'node_modules', '@sparticuz', 'chromium');
      if (fs.existsSync(path.join(cand, 'bin', 'swiftshader.tar.br'))) { pkg = cand; break; }
    }
    if (!pkg) { console.error('No Vulkan driver found and @sparticuz/chromium is not installed (npm i) – cannot get a WebGPU adapter.'); process.exit(1); }
    fs.mkdirSync(dir, { recursive: true });
    extractTar(zlib.brotliDecompressSync(fs.readFileSync(path.join(pkg, 'bin', 'swiftshader.tar.br'))), dir);
  }
  env.VK_ICD_FILENAMES = icd;
  env.LD_LIBRARY_PATH = dir + (env.LD_LIBRARY_PATH ? ':' + env.LD_LIBRARY_PATH : '');
  console.log('[launch] using SwiftShader (CPU Vulkan) from', dir);
}
const r = spawnSync(process.execPath, [script, ...process.argv.slice(3)], { stdio: 'inherit', env });
process.exit(r.status ?? 1);
