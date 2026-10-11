/**
 * Headless WebGPU verification: renders every scene through the real renderer
 * using SwiftShader, writes PNGs to tools/.cache and fails on WGSL or
 * validation errors. Re-derives the SwiftShader ICD on demand.
 *
 *   npm run verify
 */
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { brotliDecompressSync } from 'node:zlib';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const swiftshader = '/tmp/swiftshader';
const icd = `${swiftshader}/vk_swiftshader_icd.json`;

if (!existsSync(icd)) {
  const tarBr = resolve(root, 'node_modules/@sparticuz/chromium/bin/swiftshader.tar.br');
  if (!existsSync(tarBr)) {
    console.error('SwiftShader payload missing. Run: npm install');
    process.exit(1);
  }
  console.log('extracting SwiftShader…');
  mkdirSync(swiftshader, { recursive: true });
  const tar = '/tmp/swiftshader.tar';
  writeFileSync(tar, brotliDecompressSync(readFileSync(tarBr)));
  execFileSync('tar', ['-xf', tar, '-C', swiftshader]);
}

const res = spawnSync(process.execPath, [resolve(root, 'tools/run-ts.mjs'), 'src/tools/gpucheck.ts'], {
  stdio: 'inherit',
  cwd: root,
  env: {
    ...process.env,
    LD_LIBRARY_PATH: swiftshader + (process.env.LD_LIBRARY_PATH ? ':' + process.env.LD_LIBRARY_PATH : ''),
    VK_ICD_FILENAMES: icd,
  },
});
process.exit(res.status ?? 1);
