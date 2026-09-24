// Build-time only: preserve the existing SVG filters with Chromium; no browser at runtime.
// npm install --prefix .cache/browser playwright-core@1.63.0 @sparticuz/chromium@153.0.0
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {brotliDecompressSync} from 'node:zlib';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const {default:chromium}=await import(path.join(root,'.cache/browser/node_modules/@sparticuz/chromium/build/index.js'));
const {chromium:playwright}=await import(path.join(root,'.cache/browser/node_modules/playwright-core/index.mjs'));
// Use the package's bundled NSS/NSPR libraries on minimal Linux build workers.
const libs=path.join(root,'.cache/browser/libs');await mkdir(libs,{recursive:true});
const tar=path.join(root,'.cache/browser/libs.tar');
await writeFile(tar,brotliDecompressSync(await readFile(path.join(root,'.cache/browser/node_modules/@sparticuz/chromium/bin/al2023.tar.br'))));
execFileSync('tar',['xf',tar,'-C',libs]);
process.env.LD_LIBRARY_PATH=path.join(libs,'lib')+(process.env.LD_LIBRARY_PATH?':'+process.env.LD_LIBRARY_PATH:'');
const browser=await playwright.launch({executablePath:await chromium.executablePath(),args:chromium.args,headless:true});
const page=await browser.newPage();const output=path.join(root,'EngineContent/Icons/Baked');await mkdir(output,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex');const manifest={browser:browser.version(),size:256,format:'FIB1 + LE uint32 width,height,SVG-byte-count + exact SVG bytes + straight RGBA8',icons:[]};
try{
 for(const name of ['sun','moon','fog','environment-exposure','clouds','local-cloud','local-fog','folder-environment','folder-generic','folder-scene','folder-world','folder-materials']){
  const svg=await readFile(path.join(root,'EngineContent/Icons',name+'.svg'));
  let authority;
  if(['folder-scene','folder-world','folder-materials'].includes(name)){
   const source={'folder-scene':'scenes','folder-world':'models','folder-materials':'materials'}[name];
   authority=Buffer.from((await readFile(path.join(root,'Experimental/FrontierEditor/custom-icons','asset-folder-'+source+'.svg'),'utf8')).replace(/<text\b[^>]*>[\s\S]*?<\/text>/g,'').replace(/<title\b[^>]*>[\s\S]*?<\/title>/g,'').replace(/\saria-labelledby="[^"]*"/g,''));
  }else authority=await readFile(path.join(root,'Experimental/FrontierEditor',['local-cloud','local-fog','folder-environment','folder-generic'].includes(name)?'ui-icons':'custom-icons',name+'.svg'));
  if(!svg.equals(authority))throw Error('Shipped SVG differs from approved source: '+name);
  const result=await page.evaluate(async source=>{
   const img=new Image();img.src='data:image/svg+xml;base64,'+source;await img.decode();
   const canvas=document.createElement('canvas');canvas.width=canvas.height=256;
   const ctx=canvas.getContext('2d',{willReadFrequently:true});ctx.drawImage(img,0,0,256,256);
   return {pixels:Array.from(ctx.getImageData(0,0,256,256).data),png:canvas.toDataURL('image/png').split(',')[1]};
  },svg.toString('base64'));
  const header=Buffer.alloc(16);header.write('FIB1');header.writeUInt32LE(256,4);header.writeUInt32LE(256,8);header.writeUInt32LE(svg.length,12);
  const bake=Buffer.concat([header,svg,Buffer.from(result.pixels)]),png=Buffer.from(result.png,'base64');
  await writeFile(path.join(output,name+'.rgba'),bake);await writeFile(path.join(output,name+'.png'),png);
  manifest.icons.push({name,sourceSHA256:sha(svg),bakeSHA256:sha(bake),pngSHA256:sha(png)});
 }
 await writeFile(path.join(output,'NativeConstructManifest.json'),JSON.stringify(manifest,null,2)+'\n');console.log(JSON.stringify(manifest,null,2));
}finally{await browser.close()}
