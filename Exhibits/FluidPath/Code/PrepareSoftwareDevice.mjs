import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import path from 'node:path';

// Linux-only validation aid. All third-party files remain in the temporary installation directory.
const PackageRoot = process.env.BROWSER_PACKAGES_DIR || '/tmp/frontier-browser';
const Require = createRequire(path.join(PackageRoot, 'package.json'));
const Chromium = Require('@sparticuz/chromium');
const PackageFile = Require.resolve('@sparticuz/chromium');
const PackagePath = path.resolve(path.dirname(PackageFile), '../..');
const { inflate } = await import(pathToFileURL(path.join(PackagePath, 'build/esm/lambdafs.js')).href);
const Executable = await Chromium.executablePath();
const Libraries = await inflate(path.join(PackagePath, 'bin/al2023.tar.br'));
console.log(`Chromium: ${Executable}`);
console.log(`Additional libraries: ${Libraries}/lib`);
console.log('SwiftShader ICD: /tmp/vk_swiftshader_icd.json');
