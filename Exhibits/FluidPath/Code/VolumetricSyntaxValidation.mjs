import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const Html = fs.readFileSync(new URL('../../../Experimental/VolumetricFlow/index.html', import.meta.url), 'utf8');
const Scripts = [...Html.matchAll(/<script\s+type="module">([\s\S]*?)<\/script>/g)];
assert.equal(Scripts.length, 1);
new vm.Script(Scripts[0][1], { filename: 'VolumetricFlow.inline.js' });
assert.ok(Scripts[0][1].includes('const RaymarchShaderSource'));
console.log('PASS volumetric flow inline JavaScript parses, including embedded shader strings');
