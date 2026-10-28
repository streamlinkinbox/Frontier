// Optional real Chromium/WebGPU integration check; no synthetic timing samples.
import {chromium} from 'playwright';import Chromium from '@sparticuz/chromium';import {inflate} from './node_modules/@sparticuz/chromium/build/lambdafs.js';import path from 'node:path';import assert from 'node:assert/strict';import fs from 'node:fs';
const Out=process.env.OUT_DIR??'/home/user/diagnostics-proof';fs.mkdirSync(Out,{recursive:true});
const Executable=await Chromium.executablePath();await inflate(path.resolve('Tools/node_modules/@sparticuz/chromium/bin/al2023.tar.br'));
const Browser=await chromium.launch({executablePath:Executable,env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib:/tmp',VK_ICD_FILENAMES:'/tmp/vk_swiftshader_icd.json'},args:[...Chromium.args.filter(A=>!['--single-process','--in-process-gpu'].includes(A)),'--enable-unsafe-webgpu','--use-webgpu-adapter=swiftshader','--enable-features=Vulkan','--use-vulkan=swiftshader','--disable-vulkan-surface'],headless:true});
try{
 const Page=await Browser.newPage({viewport:{width:1440,height:960},deviceScaleFactor:1}),Errors=[];Page.on('pageerror',E=>Errors.push(String(E)));
 await Page.addInitScript(()=>{const RAF=requestAnimationFrame.bind(window);window.__ProofLimit=4;window.__ProofRAF=RAF;window.requestAnimationFrame=Callback=>RAF(Time=>{const H=window.IlluminationExhibit;if(H?.Surface?.Clusters)H.Surface.Clusters.DebugEnabled=true;if(H?.FrameIndex>=window.__ProofLimit)return;Callback(Time);});});
 await Page.goto('http://127.0.0.1:8080/?quality=low&scale=.25&stresspool=0&inspector=diagnostics',{waitUntil:'domcontentloaded'});
 await Page.waitForFunction(()=>window.IlluminationExhibit?.FrameIndex>=4,null,{timeout:180000,polling:100});
 await Page.evaluate(async()=>{const H=window.IlluminationExhibit;await H.Exchange.Device.queue.onSubmittedWorkDone();await H.Surface.Clusters.CollectDebug();});
 await Page.waitForFunction(()=>window.IlluminationExhibit?.Telemetry?.GpuRows.length>0,null,{timeout:30000});
 const Result=await Page.evaluate(()=>{const H=window.IlluminationExhibit,T=H.Telemetry;H.Shell.Controls.RefreshLive();H.Shell.ClusterOverlay.Draw();return {frames:T.FrameRows.length,gpuSamples:T.GpuRows.length,csv:T.CSV().slice(0,5000),clusterCandidates:H.Surface.Clusters.CandidateCount,clusterVisible:H.Surface.Clusters.DebugVisible,rawGpuKeys:Object.keys(T.GpuRows.at(-1))};});
 assert.equal(Result.frames,4);assert.ok(Result.gpuSamples>0);assert.ok(Result.csv.includes('setting.CloudResolution'));assert.ok(Result.rawGpuKeys.includes('gpu_ms.Present'));assert.ok(Result.rawGpuKeys.includes('gpu_ms.Cluster culling'));
 await Page.screenshot({path:path.join(Out,'diagnostics.png'),timeout:90000});
 await Page.locator('#inspector-content').evaluate(E=>{E.scrollTop=E.scrollHeight;});await Page.screenshot({path:path.join(Out,'diagnostics-details.png'),timeout:90000});
 await Page.evaluate(()=>window.IlluminationExhibit.Shell.Select('clusters'));
 await Page.screenshot({path:path.join(Out,'clusters.png'),timeout:90000});
 await Page.getByRole('button',{name:'64',exact:true}).click();
 // Use a real scene shape, with the real camera, rather than a fabricated projection.
 const ObjectId=await Page.evaluate(()=>{const H=window.IlluminationExhibit,E=H.Shell.Entities.find(E=>E.Id.startsWith('object-')&&H.Scene.Occupants[Number(E.Id.slice(7))].Obstacle);if(!E)throw new Error('No movable shape');H.Shell.Select(E.Id);H.Shell.Gizmo.FrameSelection();return E.Id;});
 await Page.evaluate(()=>{window.__ProofLimit++;window.IlluminationExhibit.AdvanceFrame(performance.now());});
 await Page.waitForFunction(()=>window.IlluminationExhibit.FrameIndex>=5,null,{timeout:90000});
 assert.ok(await Page.evaluate(()=>window.IlluminationExhibit.Surface.Clusters.CandidateCount)>Result.clusterCandidates,'leaf-budget control rebuilds real clusters');
 const Before=Number(await Page.getByLabel('World X',{exact:true}).inputValue());await Page.getByLabel('World X',{exact:true}).fill(String(Before+2));await Page.getByLabel('World X',{exact:true}).dispatchEvent('change');
 const After=await Page.evaluate(()=>window.IlluminationExhibit.Shell.Gizmo.Target().Get()[0]);assert.ok(Math.abs(After-Before-2)<.02);
 await Page.evaluate(()=>{window.IlluminationExhibit.Shell.Gizmo.Draw();});
 const Line=Page.locator('svg line[data-axis="0"]');await Line.waitFor();const Hit=await Line.evaluate(E=>{const B=E.ownerSVGElement.getBoundingClientRect(),X1=+E.getAttribute('x1'),X2=+E.getAttribute('x2'),Y1=+E.getAttribute('y1'),Y2=+E.getAttribute('y2');return {x:B.x+X1*.25+X2*.75,y:B.y+Y1*.25+Y2*.75,dx:X2-X1,dy:Y2-Y1};});
 await Page.mouse.move(Hit.x,Hit.y);await Page.mouse.down();await Page.mouse.move(Hit.x+Hit.dx*.4,Hit.y+Hit.dy*.4,{steps:3});await Page.mouse.up();const Dragged=await Page.evaluate(()=>window.IlluminationExhibit.Shell.Gizmo.Target().Get()[0]);assert.ok(Math.abs(Dragged-After)>.05,'axis drag must move the actual object');
 await Page.keyboard.press('g');await Page.mouse.move(Hit.x+70,Hit.y+40);await Page.keyboard.press('Escape');assert.ok(Math.abs(await Page.evaluate(()=>window.IlluminationExhibit.Shell.Gizmo.Target().Get()[0])-Dragged)<.0001);
 await Page.evaluate(()=>{window.__ProofLimit++;window.IlluminationExhibit.AdvanceFrame(performance.now());});await Page.waitForFunction(()=>window.IlluminationExhibit.FrameIndex>=6,null,{timeout:90000});
 await Page.screenshot({path:path.join(Out,'object-gizmo.png'),timeout:90000});
 const Exported=await Page.evaluate(()=>{const H=window.IlluminationExhibit,T=H.Telemetry;T.Pause();const N=T.Rows.length;T.Frame(2,1);if(T.Rows.length!==N)throw new Error('Pause leaked samples');const CSV=T.CSV();T.Dispose();H.Telemetry=null;H.Shell.Select('diagnostics');return CSV;});fs.writeFileSync(path.join(Out,'session.csv'),Exported);
 assert.equal(Errors.length,0,Errors.join('\n'));console.log(JSON.stringify({...Result,csv:undefined,object:ObjectId,numericMove:'passed',axisDrag:'passed',escapeCancel:'passed',pauseRemove:'passed',pageErrors:Errors},null,2));
}finally{await Browser.close();}
