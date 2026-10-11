// Independent reference: execute the actual JSX, not a second copy of its path formulas.
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'../../..'),out=path.join(root,'Exhibits/Gallery/SunReference');
const web=path.join(root,'Experimental/FrontierEditor');
const require=createRequire(path.join(web,'package.json'));
const {build}=require('esbuild');
const {createCanvas,loadImage,GlobalFonts}=require('@napi-rs/canvas');
fs.mkdirSync(out,{recursive:true});
GlobalFonts.registerFromPath(path.join(root,'EngineContent/Fonts/SunReference/DMSans-Regular.ttf'),'DM Sans');
const src=fs.readFileSync(path.join(root,'Experimental/FrontierEditor/src.jsx'),'utf8');
const day=src.slice(src.indexOf('function DayCurve('),src.indexOf('function WindLines('));
const source=`import React from 'react';import {renderToStaticMarkup} from 'react-dom/server';import {SunGizmo,IlluminanceCurve} from './environment-graphics.jsx';${day}\nexport function svg(which,props){return renderToStaticMarkup(React.createElement(which==='Orbit'?SunGizmo:which==='Day'?DayCurve:IlluminanceCurve,props));}`;
fs.mkdirSync(path.join(web,'.cache'),{recursive:true});
await build({stdin:{contents:source,loader:'jsx',resolveDir:web},bundle:true,platform:'node',format:'esm',packages:'external',outfile:path.join(web,'.cache/sun-reference.mjs')});
const {svg}=await import(path.join(web,'.cache/sun-reference.mjs'));
const cases=[[135,38,12,110],[230,-25,3,20],[45,0,6,0],[90,89,18,150]],metrics=[];
for(let i=0;i<cases.length;i++)for(const kind of ['Orbit','Day','Illuminance'])for(const scale of [1,2]){
 const [azimuth,elevation,time,intensity]=cases[i];let text=svg(kind,{azimuth,elevation,time,intensity,onChange:()=>{}});
 text=text.slice(text.indexOf('<svg'),text.lastIndexOf('</svg>')+6);
 const w=kind==='Day'?500:360,h=kind==='Orbit'?280:kind==='Day'?123:100;
 // Inline the actual reference's inherited text styles for a standalone SVG.
 text=text.replace('<svg ',`<svg xmlns="http://www.w3.org/2000/svg" width="${w*scale}" height="${h*scale}" style="font-family:DM Sans;font-weight:400" `)
  .replace('<defs>', '<style>.solar-unused{fill:none}</style><defs>');
 if(kind==='Orbit')text=text.replace('<g text-anchor="middle">','<g text-anchor="middle" fill="#9c9b95" font-size="10">').replaceAll('class="orbit-time"','class="orbit-time" fill="#686762" font-size="9"');
 // Skia SVG rejects CSS #RRGGBBAA; losslessly express the same alpha separately.
 text=text.replace(/(fill|stroke)="#([0-9a-fA-F]{6})([0-9a-fA-F]{2})"/g,(_,attr,rgb,alpha)=>`${attr}="#${rgb}" ${attr}-opacity="${parseInt(alpha,16)/255}"`);
 fs.writeFileSync(path.join(out,`Reference-${kind}-${i}-${scale}x.svg`),text+'\n');
 const c=createCanvas(w*scale,h*scale),cx=c.getContext('2d');cx.fillStyle='#232323';cx.fillRect(0,0,c.width,c.height);cx.drawImage(await loadImage(Buffer.from(text)),0,0);
 fs.writeFileSync(path.join(out,`Reference-${kind}-${i}-${scale}x.png`),c.toBuffer('image/png'));
 const n=createCanvas(c.width,c.height),nx=n.getContext('2d');nx.drawImage(await loadImage(path.join(out,`Native-${kind}-${i}-${scale}x.png`)),0,0);
 const a=cx.getImageData(0,0,c.width,c.height).data,b=nx.getImageData(0,0,c.width,c.height).data;let sum=0,changed=0;
 for(let p=0;p<a.length;p+=4){let delta=0;for(let k=0;k<3;k++)delta+=Math.abs(a[p+k]-b[p+k]);sum+=delta;if(delta/3>12)changed++;}
 metrics.push({kind,case:i,scale,meanRgbPercent:100*sum/(c.width*c.height*3*255),pixelsOver12Percent:100*changed/(c.width*c.height)});
}
fs.writeFileSync(path.join(out,'Comparison.json'),JSON.stringify(metrics,null,2)+'\n');
console.log(JSON.stringify(metrics,null,2));
// A regression gate for isolated drawings, NOT whole-inspector 1:1 acceptance.
const failures=metrics.filter(m=>m.meanRgbPercent>0.35||m.pixelsOver12Percent>1.5);
fs.writeFileSync(path.join(out,'ComparisonStatus.txt'),`Isolated SVG drawing regression: ${failures.length?'FAIL':'PASS'} (${metrics.length} comparisons).\nGate: mean RGB error <=0.35%; pixels over 12/255 channel-average error <=1.5%.\nNot pixel identity or full inspector acceptance.\n`);
if(failures.length)process.exitCode=1;
