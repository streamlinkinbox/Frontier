// Independent SVG reference, not an image of the React interface.
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'../../..'),out=path.join(root,'Exhibits/Gallery/IconArt');
const web=path.join(root,'Experimental/FrontierEditor');
const require=createRequire(path.join(web,'package.json'));
const {createCanvas,loadImage}=require('@napi-rs/canvas');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'EngineContent/Icons/Manifest.json'),'utf8'));
const lines=fs.readFileSync(path.join(out,'IconResults.tsv'),'utf8').trim().split('\n').slice(1);
const outcomes=new Map(lines.map(line=>{const[symbol,file,result,substitute,...diagnostic]=line.split('\t');return[symbol,{result,diagnostic:diagnostic.join('\t')==='-'?'':diagnostic.join('\t')}]}));
const comparison=[];
fs.mkdirSync(path.join(out,'Reference'),{recursive:true});
for(const icon of manifest){
 const ref=createCanvas(128,128),r=ref.getContext('2d');const svg=fs.readFileSync(path.join(root,'EngineContent/Icons',icon.file),'utf8').replace(/<svg\b[^>]*>/,tag=>tag.replace(/\s(?:width|height)="[^"]*"/g,'').replace('>',' width="128" height="128">'));r.drawImage(await loadImage(Buffer.from(svg)),0,0,128,128);
 fs.writeFileSync(path.join(out,'Reference',icon.symbol+'.png'),ref.toBuffer('image/png'));
 const actual=createCanvas(128,128),a=actual.getContext('2d');a.drawImage(await loadImage(path.join(out,'Raster',icon.symbol+'.png')),0,0);
 const R=r.getImageData(0,0,128,128).data,A=a.getImageData(0,0,128,128).data;
 let total=0,alpha=0,changed=0;
 for(let i=0;i<R.length;i+=4){let pixel=0;for(let c=0;c<3;c++)pixel+=Math.abs(R[i+c]*R[i+3]/255-A[i+c]*A[i+3]/255);const da=Math.abs(R[i+3]-A[i+3]);pixel+=da;alpha+=da;total+=pixel;if(pixel/4>12)changed++;}
 const mae=total/(128*128*4*255),alphaMAE=alpha/(128*128*255),different=changed/(128*128);
 const outcome=outcomes.get(icon.symbol);
 // Diagnostic threshold only: no unsupported artwork is promoted to approved by a low pixel difference.
 const fidelity=outcome.result!=='ready'?'BLOCKED':mae>.02||different>.1?'REVIEW':'WITHIN_TOLERANCE';
 comparison.push({...icon,...outcome,mae,alphaMAE,different,fidelity});
}
fs.writeFileSync(path.join(out,'Compatibility.json'),JSON.stringify({reference:'@napi-rs/canvas SVG decode; exact EngineContent/Icons files, 128px, premultiplied RGBA comparison',limits:{mae:.02,different:.1,channelDifference:12},icons:comparison},null,2)+'\n');
fs.writeFileSync(path.join(out,'Compatibility.tsv'),'symbol\tThorVG result\tmean absolute RGBA error\talpha error\tchanged pixels\tdecision\tdiagnostic\n'+comparison.map(i=>`${i.symbol}\t${i.result}\t${(i.mae*100).toFixed(3)}%\t${(i.alphaMAE*100).toFixed(3)}%\t${(i.different*100).toFixed(2)}%\t${i.fidelity}\t${i.diagnostic||'-'}`).join('\n')+'\n');
const choices=['CollectionBracketedObjects','FolderEnvironment','OutlinerStars','OutlinerPrecipitation','LocalCloud','LocalFog','Sun','Moon','SkyScattering','Clouds','LensFlare','Terrain','Camera','EditorMaterial','FolderWorld','SlateLantern','Stars','WeatherRain'];
const sheet=createCanvas(1080,110+Math.ceil(choices.length/3)*242),ctx=sheet.getContext('2d');ctx.fillStyle='#191b1e';ctx.fillRect(0,0,sheet.width,sheet.height);ctx.fillStyle='#e5e7ea';ctx.font='22px sans-serif';ctx.fillText('ThorVG CPU · SVG compatibility proof',24,35);ctx.font='12px sans-serif';ctx.fillStyle='#aab0ba';ctx.fillText('Left: independent SVG reference. Right: actual IconArt diagnostic raster. No GPU or React screenshots.',24,60);ctx.fillText('Unsupported artwork is BLOCKED in strict mode, even when the diagnostic preview looks similar.',24,80);
for(let i=0;i<choices.length;i++){
 const name=choices[i],entry=comparison.find(x=>x.symbol===name),x=(i%3)*360+20,y=110+Math.floor(i/3)*242;
 ctx.fillStyle='#e1e2e6';ctx.font='14px sans-serif';ctx.fillText(name,x,y+15);
 for(const [j,folder] of ['Reference','Raster'].entries()){
  ctx.fillStyle='#22262b';ctx.fillRect(x+j*158,y+27,144,144);ctx.drawImage(await loadImage(path.join(out,folder,name+'.png')),x+8+j*158,y+35);
  ctx.fillStyle='#949ca8';ctx.font='10px sans-serif';ctx.fillText(j?'THORVG CPU':'SVG REFERENCE',x+j*158,y+187);
 }
 ctx.fillStyle=entry.fidelity==='WITHIN_TOLERANCE'?'#94c8ac':'#e3aa7e';ctx.font='11px sans-serif';ctx.fillText(`${entry.fidelity} · mean error ${(entry.mae*100).toFixed(2)}%`,x,y+208);
 if(entry.diagnostic){ctx.fillStyle='#a79b91';ctx.font='9px sans-serif';ctx.fillText(entry.diagnostic.slice(0,57),x,y+224);}
}
fs.writeFileSync(path.join(out,'IconArtComparison.png'),sheet.toBuffer('image/png'));
const summary=createCanvas(1080,110+3*242);summary.getContext('2d').drawImage(sheet,0,0);fs.writeFileSync(path.join(out,'IconArtSummary.png'),summary.toBuffer('image/png'));
console.log(JSON.stringify(comparison.reduce((a,i)=>(a[i.fidelity]=(a[i.fidelity]||0)+1,a),{})));

if(comparison.some(i=>i.fidelity==='REVIEW')){console.error('An admitted SVG exceeds the fidelity limits.');process.exitCode=1;}
