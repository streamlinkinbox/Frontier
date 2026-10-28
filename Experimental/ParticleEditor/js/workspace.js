export const LightningParameterRanges={intervalMin:[.2,30],intervalMax:[.2,60],distance:[50,6000],amp:[.2,5],levels:[3,7],branchChance:[0,1],width:[.02,.35],brightness:[0,3],cloudGlow:[0,3]};
export function AdmitLightningParameters(Target,Source){
 if(!Source||typeof Source!=='object'||Array.isArray(Source))return;
 if(typeof Source.auto==='boolean')Target.auto=Source.auto;
 for(const [K,[Lo,Hi]]of Object.entries(LightningParameterRanges))if(typeof Source[K]==='number'&&Number.isFinite(Source[K]))Target[K]=Math.max(Lo,Math.min(Hi,Source[K]));
 Target.levels=Math.round(Target.levels);Target.intervalMax=Math.max(Target.intervalMin,Target.intervalMax);
}
export function AdmitParticleParameters(Target,Source,Depth=0){
 if(!Source||typeof Source!=='object'||Depth>8)return;
 for(const [Key,Was]of Object.entries(Target)){
  if(!Object.hasOwn(Source,Key)||['__proto__','constructor','prototype'].includes(Key))continue;const Value=Source[Key];
  if(Array.isArray(Was)){
   if(!Array.isArray(Value)||Value.length>64||Was.length&&Value.length!==Was.length)continue;
   if(Value.every(V=>typeof V==='number'&&Number.isFinite(V))||Was.length===0&&Value.every(V=>typeof V==='string'))Target[Key]=Value.slice();
  }else if(Was&&typeof Was==='object'){AdmitParticleParameters(Was,Value,Depth+1);}
  else if(typeof Was==='number'&&typeof Value==='number'&&Number.isFinite(Value))Target[Key]=Value;
  else if(typeof Was==='boolean'&&typeof Value==='boolean')Target[Key]=Value;
  else if(typeof Was==='string'&&typeof Value==='string')Target[Key]=Value.slice(0,256);
 }
}
// Host integration only. The original reference is kept byte-for-byte under Upstream/.
export function CaptureParticleDocument(S){
 return {Format:'frontier-particle-scene',Version:1,Systems:S.systems.map(X=>({Preset:X.presetId,Name:X.name,Settings:structuredClone(X.p)})),Forces:structuredClone(S.forces),Wind:structuredClone(S.wind),Camera:structuredClone(S.cam),Web:{enabled:S.web.enabled,interval:S.web.interval,k:S.web.k},Playing:S.playing,TimeScale:S.timeScale,Selection:S.selection.type==='system'?S.systems.findIndex(X=>X.id===S.selection.id):null,SelectionKind:S.selection.type,Lightning:S.lightning?Object.fromEntries(['auto','intervalMin','intervalMax','distance','amp','levels','branchChance','width','brightness','cloudGlow'].map(K=>[K,S.lightning[K]])):undefined};
}
export function InstallWorkspace(PE,State,Api){
 const Query=new URLSearchParams(location.search),Token=Query.get('workspace-token');
 if(!Token||window.parent===window)return;
 const Post=(Type,Payload,Request)=>window.parent.postMessage({Frontier:'particle-workspace',Version:1,Token,Editor:'particles',Type,Payload,Request},location.origin);
 let Last='',Timer;
 const Publish=Request=>{const Doc=CaptureParticleDocument(State),Text=JSON.stringify(Doc);if(Text===Last&&!Request)return;Last=Text;try{localStorage.setItem('Frontier.ParticleWorkspace.particles.v1',Text);}catch{Post('storage-error','Browser storage is unavailable; export the project.');}Post('document',Doc,Request);};
 PE.Edits.Subscribe(()=>{clearTimeout(Timer);Timer=setTimeout(()=>Publish(),180);});
 window.addEventListener('message',E=>{const M=E.data;if(E.origin!==location.origin||E.source!==window.parent||M?.Frontier!=='particle-workspace'||M.Token!==Token||M.Version!==1)return;
  if(M.Type==='save'){Publish(M.Request);return;}
  if(M.Type==='restore')try{if(M.Payload)Api.Restore(M.Payload);Last='';Publish();}catch(Error){Post('error',String(Error.message??Error));}
 });
 setInterval(()=>{Publish();Post('metrics',{preview_fps_smoothed:State.fps,preview_frame_ms:State.frameMs,cpu_ms:State.cpuMs,systems:State.systems.length,canvas_width:State.engine.canvas.width,canvas_height:State.engine.canvas.height,render_api:'webgpu',time_scale:State.timeScale,particle_capacity:State.systems.reduce((N,S)=>N+S.p.capacity,0),frame_id:State.frame,editor:'particles'});},1000);
 window.addEventListener('pagehide',()=>Publish());
 window.addEventListener('error',E=>Post('error',E.message));
 window.addEventListener('unhandledrejection',E=>Post('error',String(E.reason)));
 Post('ready');
}
