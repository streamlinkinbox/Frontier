import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {EnvironmentIconURLs} from '../Source/EnvironmentIconAssets.js';
test('every environment icon is embedded intact, including the new Stars constellation',()=>{
 const Folder=new URL('../Assets/EnvironmentIcons/',import.meta.url);
 for(const File of fs.readdirSync(Folder).filter(N=>N.endsWith('.svg'))){const URL_=EnvironmentIconURLs[File.slice(0,-4)];assert.ok(URL_?.startsWith('data:image/svg+xml;'),File);assert.equal(decodeURIComponent(URL_.split(',')[1]),fs.readFileSync(new URL(File,Folder),'utf8'),File);}
 for(const Key of ['sun','clouds','local-cloud','sky-scattering','outliner-stars','moon','lens-flare','fog','folder-environment'])assert.ok(EnvironmentIconURLs[Key],Key);
});
