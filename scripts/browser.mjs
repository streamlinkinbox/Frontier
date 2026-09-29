import fs from 'node:fs';
import zlib from 'node:zlib';
import { execFileSync } from 'node:child_process';
import chromium from '@sparticuz/chromium';
import { chromium as playwright } from 'playwright';

// Portable software-WebGL browser for asset export and automated visual checks.
export async function launchBrowser() {
  if (!fs.existsSync('/tmp/frontier-browser/lib/libnspr4.so')) {
    fs.mkdirSync('/tmp/frontier-browser', { recursive: true });
    const path = new URL('../node_modules/@sparticuz/chromium/bin/al2023.tar.br', import.meta.url);
    fs.writeFileSync('/tmp/frontier-al2023.tar', zlib.brotliDecompressSync(fs.readFileSync(path)));
    execFileSync('tar', ['xf', '/tmp/frontier-al2023.tar', '-C', '/tmp/frontier-browser']);
  }
  return playwright.launch({
    executablePath: await chromium.executablePath(),
    args: chromium.args,
    headless: true,
    env: { ...process.env, LD_LIBRARY_PATH: `/tmp/frontier-browser/lib:${process.env.LD_LIBRARY_PATH || ''}` }
  });
}
