import {CloudDefaults} from './CloudSettings.js';
import {CloudDensity} from './CloudField.js';
// Bounded once-per-strike search in the SAME baked density field as the renderer.
// No cloud parameter edits, no new light-cache volume and no per-frame search.
export function FindLightningCloudAnchor(Settings,Requested,Uniform){
 const S={...CloudDefaults,...Settings};
 const Global=S.CloudEnabled&&S.CloudDensity>0&&S.CloudCoverage>0&&S.CloudCeiling>S.CloudBase;
 const Local=S.LocalCloudEnabled&&S.LocalCloudDensity>0&&S.LocalCloudCoverage>0;
 const Want=S.LightningCloudTarget??0,UseLocal=Local&&(Want===2||!Global&&Want!==1);
 if(!UseLocal&&(!Global||Want===2))return {Position:Requested.slice(),Medium:'none',Density:0,Embedded:false};
 const Motion=Uniform?Array.from(Uniform.slice(UseLocal?40:24,UseLocal?43:27)):[0,0,0];
 const Centre=UseLocal?[S.LocalCloudX,S.LocalCloudY,S.LocalCloudZ]:Requested;
 const Depth=UseLocal?S.LocalCloudHalfZ*2:Math.min(S.CloudCeiling,S.CloudBase+S.CloudThickness)-S.CloudBase;
 const Span=UseLocal?[S.LocalCloudHalfX*.55,S.LocalCloudHalfY*.55]:[Math.min(500,Math.max(60,(S.LightningDistance??1800)*.2)),Math.min(500,Math.max(60,(S.LightningDistance??1800)*.2))];
 let Best={Position:Centre.slice(),Medium:UseLocal?'local':'global',Density:0,Embedded:false};
 const Offsets=[[0,0],[1,0],[-1,0],[0,1],[0,-1],[.7,.7],[-.7,-.7],[.7,-.7],[-.7,.7]];
 for(const H of [.35,.2,.55])for(const [X,Y]of Offsets){
  const P=[Centre[0]+X*Span[0],Centre[1]+Y*Span[1],UseLocal?S.LocalCloudZ-S.LocalCloudHalfZ+H*Depth:S.CloudBase+H*Depth];
  const Density=CloudDensity(S,P,UseLocal,Motion);
  if(Density>Best.Density)Best={...Best,Position:P,Density,Embedded:Density>.015};
  // Prefer a genuinely occupied near-centre sample over moving arbitrarily to the densest cloud.
  if(Density>=.12)return Best;
 }
 return Best;
}
