import {chromium} from 'playwright';import Chromium from '@sparticuz/chromium';import {inflate} from './node_modules/@sparticuz/chromium/build/lambdafs.js';import path from 'node:path';import fs from 'node:fs';import assert from 'node:assert/strict';
const Out=process.env.OUT_DIR??'/home/user/particle-workspace-proof';fs.mkdirSync(Out,{recursive:true});const Executable=await Chromium.executablePath();await inflate(path.resolve('Tools/node_modules/@sparticuz/chromium/bin/al2023.tar.br'));
const Browser=await chromium.launch({executablePath:Executable,env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib:/tmp',VK_ICD_FILENAMES:'/tmp/vk_swiftshader_icd.json'},args:[...Chromium.args.filter(A=>!['--single-process','--in-process-gpu'].includes(A)),'--enable-unsafe-webgpu','--use-webgpu-adapter=swiftshader','--enable-features=Vulkan','--use-vulkan=swiftshader','--disable-vulkan-surface','--enable-unsafe-swiftshader'],headless:true});
try{
 const Page=await Browser.newPage({viewport:{width:1440,height:900},acceptDownloads:true}),Errors=[],ConsoleErrors=[];Page.on('pageerror',E=>{Errors.push(String(E));console.log('PAGE ERROR',String(E));});Page.on('console',M=>{if(M.type()==='error'){ConsoleErrors.push(M.text());console.log('CONSOLE ERROR',M.text());}});
 await Page.addInitScript(()=>{const RAF=requestAnimationFrame.bind(window);window.__ProofLimit=1;window.requestAnimationFrame=Cb=>RAF(T=>{const H=window.IlluminationExhibit;if(H?.FrameIndex>=window.__ProofLimit&&!H?.ParticleWorkspace?.Active)return;Cb(T);});});
 await Page.goto('http://127.0.0.1:8080/?quality=low&scale=.25&stresspool=0&inspector=particles',{waitUntil:'domcontentloaded'});
 await Page.waitForFunction(()=>window.IlluminationExhibit?.FrameIndex>=1,null,{timeout:180000,polling:100});
 console.log('Host frame rendered; opening particles');await Page.getByRole('button',{name:'Open Particle Editor',exact:true}).click();
 await Page.waitForFunction(()=>window.IlluminationExhibit.ParticleWorkspace.Ready,null,{timeout:120000,polling:100});
 console.log('Particle editor ready');const Particles=Page.frames().find(F=>F.url().includes('/Working/ParticleEditor/'));assert.ok(Particles);
 await Particles.waitForFunction(()=>window.ParticleEditorState?.frame>=3,null,{timeout:120000,polling:100});
 assert.equal(await Particles.locator('#boot-error').isVisible(),false);
 await Page.evaluate(()=>{const H=window.IlluminationExhibit,N=H.FrameIndex;H.AdvanceFrame(performance.now());if(H.FrameIndex!==N)throw new Error('Driving scene rendered while particle workspace active');});
 await Particles.getByRole('button',{name:/^Sparks/}).click();
 const Rate=Particles.getByRole('slider',{name:'Rate',exact:true});await Rate.fill('321');await Rate.dispatchEvent('input');
 await Page.waitForFunction(()=>window.IlluminationExhibit.ParticleWorkspace.Documents.particles?.Systems[0]?.Settings.rate===321,null,{timeout:15000,polling:100});
 await Page.screenshot({path:path.join(Out,'particle-editor.png')});
 // Invalid source/token must not modify the retained document.
 await Page.evaluate(()=>{const W=window.IlluminationExhibit.ParticleWorkspace;window.dispatchEvent(new MessageEvent('message',{origin:location.origin,source:window,data:{Frontier:'particle-workspace',Version:1,Token:W.Token,Editor:'particles',Type:'document',Payload:{Format:'frontier-particle-scene',Version:1,Systems:[]}}}));});
 assert.equal(await Page.evaluate(()=>window.IlluminationExhibit.ParticleWorkspace.Documents.particles.Systems.length),1);
 await Page.getByRole('tab',{name:'Flux · Particle Studio',exact:true}).click();
 await Page.waitForFunction(()=>{const W=window.IlluminationExhibit.ParticleWorkspace;return W.Editor==='flux'&&W.Ready&&W.Documents.flux;},null,{timeout:120000,polling:100});
 const Flux=Page.frames().find(F=>F.url().includes('/Published/FluxParticleStudio/'));assert.ok(Flux);await Flux.waitForFunction(()=>window.__flux?.engine?.debug?.report().tris>0,null,{timeout:60000,polling:100});
 await Flux.locator('#comp-name').fill('Workspace round-trip');await Flux.locator('#comp-name').dispatchEvent('change');
 await Page.waitForFunction(()=>window.IlluminationExhibit.ParticleWorkspace.Documents.flux?.comp?.name==='Workspace round-trip',null,{timeout:15000,polling:100});
 await Page.screenshot({path:path.join(Out,'flux-particle-studio.png')});
 await Page.getByRole('tab',{name:'Particle Editor',exact:true}).click();
 await Page.waitForFunction(()=>{const W=window.IlluminationExhibit.ParticleWorkspace;return W.Editor==='particles'&&W.Ready&&W.Documents.particles?.Systems[0]?.Settings.rate===321;},null,{timeout:120000,polling:100});
 const Reopened=Page.frames().find(F=>F.url().includes('/Working/ParticleEditor/'));await Reopened.waitForFunction(()=>window.ParticleEditorState.systems[0]?.p.rate===321,null,{timeout:30000,polling:100});
 const Download=Page.waitForEvent('download');await Page.getByRole('button',{name:'Export project',exact:true}).click();const File=await Download;await File.saveAs(path.join(Out,'frontier-particles.json'));const Doc=JSON.parse(fs.readFileSync(path.join(Out,'frontier-particles.json'),'utf8'));assert.equal(Doc.Editors.particles.Systems[0].Settings.rate,321);assert.equal(Doc.Editors.flux.comp.name,'Workspace round-trip');
 Doc.Editors.particles.Systems[0].Settings.rate=222;await Page.evaluate(D=>window.IlluminationExhibit.ParticleWorkspace.Import(D),Doc);await Reopened.waitForFunction(()=>window.ParticleEditorState.systems[0]?.p.rate===222,null,{timeout:30000,polling:100});
 await Page.getByRole('button',{name:'← Back to scene',exact:true}).click();await Page.waitForFunction(()=>!window.IlluminationExhibit.ParticleWorkspace.Active,null,{polling:100});assert.equal(await Page.locator('.particle-workspace iframe').count(),0);
 await Page.evaluate(()=>{window.__ProofLimit=2;window.IlluminationExhibit.AdvanceFrame(performance.now());});await Page.waitForFunction(()=>window.IlluminationExhibit.FrameIndex>=2,null,{timeout:120000,polling:100});
 const Result=await Page.evaluate(()=>({sceneFrames:window.IlluminationExhibit.FrameIndex,particleSystems:window.IlluminationExhibit.ParticleWorkspace.Documents.particles.Systems.length,fluxLayers:window.IlluminationExhibit.ParticleWorkspace.Documents.flux.layers.length,workspaceEvents:window.IlluminationExhibit.Telemetry.EventRows.filter(R=>R.event?.startsWith('particle')).length}));
 console.log(JSON.stringify({...Result,liveEdit:'passed',switchRestore:'passed',exportImport:'passed',scenePauseResume:'passed',singleMountedIframe:'passed',pageErrors:Errors,consoleErrors:ConsoleErrors},null,2));assert.equal(Errors.length,0);assert.equal(ConsoleErrors.length,0);
}finally{await Browser.close();}
