import {PrepareCsvDownload} from './CsvDownload.js';
import {SessionTelemetry} from './SessionTelemetry.js';
const Colour=['#69dfa5','#78aaff','#ffcf73','#ef849c','#b494f4'];
function Plot(C,Card,Rows,Series){
 C.Instrument(Card,120,(X,W,H)=>{
  const R=Rows().slice(-240);X.fillStyle='#11181c';X.fillRect(0,0,W,H);if(!R.length)return;
  const Max=Math.max(1,...R.flatMap(V=>Series.map(([K])=>Number(V[K])||0)))*1.15;
  X.font='18px monospace';X.fillStyle='#8796a2';X.fillText(Max.toFixed(2)+' ms',12,24);
  for(let J=1;J<4;J++){X.strokeStyle='#ffffff12';X.beginPath();X.moveTo(0,H*J/4);X.lineTo(W,H*J/4);X.stroke();}
  Series.forEach(([K],I)=>{X.beginPath();X.strokeStyle=Colour[I%Colour.length];R.forEach((V,N)=>{const Y=H-15-(Number(V[K])||0)/Max*(H-45),P=N/Math.max(1,R.length-1)*W;N?X.lineTo(P,Y):X.moveTo(P,Y);});X.stroke();});
 },true);
 C.Muted(Card,Series.map(([,L],I)=>`${['●','◆','▲','■','◇'][I%5]} ${L}`).join(' · '));
}
export function BuildDiagnostics(H,C,B){
 const T=H.Telemetry,Top=C.Card(B,'Session recorder','Activity',{Accent:Colour[0],Index:1});
 if(!T){C.Muted(Top,'Recorder removed. No session hooks or retained data remain.');C.Button(Top,'Install recorder','Activity',()=>{H.Telemetry=new SessionTelemetry(H);H.Shell.Select('diagnostics');});return;}
 const RefreshToggle=C.Button(Top,H.Settings.EditorLiveRefresh===false?'Resume live editor refresh':'Pause live editor refresh (A/B)','Monitor',()=>{H.Settings.EditorLiveRefresh=H.Settings.EditorLiveRefresh===false;RefreshToggle.textContent=H.Settings.EditorLiveRefresh?'Pause live editor refresh (A/B)':'Resume live editor refresh';T.Event('editor_live_refresh',{enabled:H.Settings.EditorLiveRefresh});T.Snapshot(true);});
 C.Muted(Top,'A/B switch pauses live readouts, charts, outliner and gizmo redraw, not the renderer or recorder. It does not remove the DOM or all browser UI cost.');
 const Controls=C.ActionRow(Top);C.Button(Controls,'Record / pause','Activity',()=>T.Recording?T.Pause():T.Resume());C.Button(Controls,'Clear','RotateCcw',()=>T.Clear());
 C.Readout(Top,()=>[T.Status,`${T.Rows.length.toLocaleString()} / ${T.Limit.toLocaleString()} records · ${(performance.now()-T.Start)/1000|0}s session`,H.Metrics.Enabled?'Raw GPU samples collected asynchronously':'GPU timestamps unavailable — not reported as zero']);
 const Export=C.ActionRow(Top),Status=document.createElement('p');Top.appendChild(Status);
 C.Button(Export,'Copy all CSV','Copy',async()=>{try{await navigator.clipboard.writeText(T.CSV());Status.textContent='All recorded rows copied as CSV.';}catch{const Text=document.createElement('textarea');Text.className='telemetry-select';Text.readOnly=true;Text.rows=7;Text.value=T.CSV();Status.replaceChildren();Status.textContent='Clipboard blocked: press Ctrl+C / Cmd+C to copy all selected CSV below.';Status.appendChild(Text);Text.focus();Text.select();}});
 let DownloadHandle;
 const Download=C.Button(Export,'Download CSV','Download',async()=>{
  Download.disabled=true;Status.textContent='Preparing complete session CSV…';
  try{await new Promise(Resolve=>setTimeout(Resolve,0));const Csv=T.CSV();DownloadHandle?.Dispose();DownloadHandle=await PrepareCsvDownload(Csv,Status);DownloadHandle.Link.click();}
  catch(Error){Status.textContent='CSV export failed: '+String(Error.message??Error)+'. Try Copy all CSV.';}
  finally{Download.disabled=false;}
 });
 C.Button(Top,'Remove recorder & discard data','X',()=>{T.Dispose();H.Telemetry=null;H.Shell.Select('diagnostics');});
 C.Muted(Top,'In memory until export; reload loses the session. Storage stops at the limit, never silently overwrites. Charts show recent samples; CSV includes everything, including settings and events.');
 const Frames=()=>T.FrameRows,Gpu=()=>T.GpuRows;
 const Frame=C.Card(B,'Frame pacing','Activity',{Accent:Colour[0],Index:2});
 C.Readout(Frame,()=>{const R=Frames().slice(-600).map(V=>V.frame_ms).filter(V=>V>0).sort((A,B)=>A-B);return R.length?[`Median ${R[R.length>>1].toFixed(2)} ms · p99 ${R[Math.min(R.length-1,Math.floor(R.length*.99))].toFixed(2)} ms`,`Recent frames >16.67 ms: ${R.filter(V=>V>16.67).length} / ${R.length}`]:['Waiting for frame samples'];});
 Plot(C,Frame,Frames,[['frame_ms','Frame interval'],['cpu_submit_ms','CPU update + encode'],['ui_js_ms','Editor JS']]);
 const Breakdown=C.Card(B,'CPU attribution · latest frame','Activity',{Index:3});
 C.Readout(Breakdown,()=>{const R=T.FrameRows.at(-1);return R?Object.entries(R).filter(([K])=>K.startsWith('cpu_phase_ms.')||K.startsWith('ui_phase_ms.')).map(([K,V])=>`${K.replace('cpu_phase_ms.','CPU / ').replace('ui_phase_ms.','UI / ')}: ${V.toFixed(3)} ms`):['Waiting for CPU phase samples'];});
 C.Muted(Breakdown,'UI subphases are nested inside CPU / hud_editor, not additional costs. Physics/effects is an aggregate; steps and GPU queue skips are also exported.');
 const Ui=C.Card(B,'Editor & recorder CPU','Monitor',{Accent:Colour[2],Index:3});
 Plot(C,Ui,Frames,[['ui_js_ms','Editor refresh'],['telemetry_snapshot_ms','Settings capture'],['telemetry_prior_record_ms','Prior record append']]);
 C.Muted(Ui,'JavaScript duration only. Inspector-build events and page long tasks are in CSV. Layout, paint, compositor work and uninstrumented input handlers are not attributed as UI time. Recorder values are partial self-timings, not its complete system overhead.');
 const Pass=C.Card(B,'GPU workload','Layers',{Accent:Colour[1],Index:4});
 const Choice=document.createElement('select');Choice.className='telemetry-select';for(const Name of H.Metrics.PassNames){const O=document.createElement('option');O.value=Name;O.textContent=Name;Choice.appendChild(O);}Choice.value='Clouds';Pass.appendChild(Choice);
 C.Instrument(Pass,100,(X,W,Height)=>{const R=Gpu().slice(-240),Key='gpu_ms.'+Choice.value,Max=Math.max(.01,...R.map(V=>V[Key]??0))*1.1;X.fillStyle='#11181c';X.fillRect(0,0,W,Height);X.fillStyle=Colour[1];X.font='18px monospace';X.fillText(Choice.value+' · '+Max.toFixed(3)+' ms scale',10,24);X.beginPath();let Gap=true;R.forEach((V,I)=>{if(V[Key]==null){Gap=true;return;}const P=I/Math.max(1,R.length-1)*W,Y=Height-10-V[Key]/Max*(Height-40);Gap?X.moveTo(P,Y):X.lineTo(P,Y);Gap=false;});X.strokeStyle=Colour[1];X.stroke();},true);
 C.Readout(Pass,()=>{const Recent=Gpu().slice(-120);return H.Metrics.PassNames.map(Name=>{const A=Recent.map(R=>R['gpu_ms.'+Name]).filter(V=>V!=null);return [Name,A.length?A.reduce((S,V)=>S+V,0)/A.length:null];}).filter(([,V])=>V!==null).sort((A,B)=>B[1]-A[1]).map(([N,V])=>`${N.padEnd(24)} ${V.toFixed(3)} ms`);});
 C.Instrument(Pass,220,(X,W,Height)=>{const R=Gpu().slice(-80),Names=H.Metrics.PassNames,Row=(Height-8)/Names.length;X.fillStyle='#11181c';X.fillRect(0,0,W,Height);X.font='18px monospace';Names.forEach((N,J)=>{X.fillStyle='#aebbc5';X.fillText(N.slice(0,23),4,J*Row+Row*.8);const Key='gpu_ms.'+N,Max=Math.max(.001,...R.map(V=>V[Key]??0));R.forEach((V,I)=>{const Value=V[Key];X.fillStyle=Value==null?'#263039':`hsl(${145-145*Value/Max} 65% 50%)`;X.fillRect(280+I/80*(W-280),J*Row,(W-280)/80-1,Row-2);});});},true);
 C.Muted(Pass,'Heatmap: recent samples left to right; green → red relative to each pass maximum; grey = unavailable.');
 C.Muted(Pass,'Per-pass raw available samples, not total frame GPU time. Missing samples are gaps. Cloud presentation is inside the existing display pass.');
 const Settings=C.Card(B,'Capture context','Monitor',{Accent:Colour[2],Index:5});
 C.Readout(Settings,()=>{const R=T.EventRows.findLast(V=>V.kind==='settings');return R?[`Settings revision ${R.settings_revision}`,`Quality: ${R['setting.quality_name']} · Render scale: ${R['setting.RenderScale']}`,`Screen: ${R['setting.screen_extent']} · Clouds: ${R['setting.cloud_extent']}`,`Cloud steps: ${R['setting.CloudSteps']} · Cloud resolution: ${R['setting.CloudResolution']}`]:['No settings snapshot'];});
 const Events=C.Card(B,'Settings & session events','List',{Accent:Colour[2],Index:6});
 C.Readout(Events,()=>T.EventRows.slice(-10).map(R=>`${(R.time_ms/1000).toFixed(1)}s · ${R.kind==='settings'?'Settings revision '+R.settings_revision:R.event}`));
 C.Muted(Events,'CSV settings rows preserve all host settings, quality and actual screen/cloud dimensions. Frame/GPU rows reference their settings revision. Browser long tasks are observed where supported; editor JS timing does not include layout, paint, compositor time or all input handlers.');
}
export function BuildClusterInspector(H,C,B){
 const G=H.Surface.Clusters,Card=C.Card(B,'Geometry clusters','Layers',{Accent:Colour[1],Index:1});
 C.Switch(Card,H.Settings,'ClusteredGeometryEnabled',{Label:'GPU cluster culling',Detail:'Lossless original triangles; not simplified Nanite LOD'});
 const Budget={Triangles:G.TriangleLimit??128};C.Segmented(Card,Budget,'Triangles',[["64",64],["128",128],["256",256]],{Label:'Maximum triangles per leaf · rebuilds',OnChange:V=>{G.TriangleLimit=V;G.Key=null;G.Cache.clear();H.Telemetry?.Event('cluster_leaf_budget',{triangles:V});}});
 const Local={Overlay:!!G.DebugEnabled};C.Switch(Card,Local,'Overlay',{Label:'Cluster bounds overlay',Detail:'Green: GPU-visible · red: GPU-culled · grey: pending',OnChange:V=>{G.DebugEnabled=V;H.Telemetry?.Event('cluster_overlay',{enabled:V});}});
 // EditorControls reports changes centrally; live sync also handles this local-only switch.

 C.Button(Card,'Rebuild clusters','RotateCcw',()=>{G.Key=null;G.Cache.clear();H.Telemetry?.Event('cluster_rebuild_requested');});
 C.Readout(Card,()=>[`${G.CandidateCount} candidates · ${G.IndexCount??0} indices · revision ${G.Revision}`,H.Settings.ClusteredGeometryEnabled===false?'Legacy drawing enabled — GPU cluster visibility is not updating':G.DebugValues?`${G.DebugVisible} GPU-visible · ${G.DebugValues.length-G.DebugVisible} culled · sampled revision ${G.DebugRevision}`:'Enable overlay for asynchronous GPU visibility readback',`Overlay: ${Math.min(G.CandidateCount,2000)} / ${G.CandidateCount} bounds (display cap only)`,G.DebugError??'']);
 C.Muted(Card,'Bounds are cluster AABBs, not triangle wireframes. Visibility comes from actual indirect GPU commands, sampled at most 4 Hz. Move the camera to watch culling; rebuild refreshes partitions. Hidden clusters may lie outside the viewport. Overlay/readback has an optional diagnostic cost.');
}
