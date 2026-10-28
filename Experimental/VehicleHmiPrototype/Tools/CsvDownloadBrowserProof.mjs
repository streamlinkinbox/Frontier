// Exercise the actual diagnostics button in an iframe without loading the GPU scene.
import {inflate} from './node_modules/@sparticuz/chromium/build/lambdafs.js';import path from 'node:path';
import {chromium} from 'playwright';import Chromium from '@sparticuz/chromium';import assert from 'node:assert/strict';import fs from 'node:fs/promises';
const Executable=await Chromium.executablePath();await inflate(path.resolve('Tools/node_modules/@sparticuz/chromium/bin/al2023.tar.br'));
const Browser=await chromium.launch({executablePath:Executable,env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib:/tmp'},args:Chromium.args.filter(A=>!['--disable-web-security','--single-process','--in-process-gpu'].includes(A)),headless:true});
const Fixture=`<!doctype html><html><head><meta charset="utf-8"></head><body><main id="inspector"></main><script type="module">
import {BuildDiagnostics} from '/Source/DiagnosticsInspector.js';
import {EditorControls} from '/Source/EditorControls.js';
import {SessionTelemetry} from '/Source/SessionTelemetry.js';
const H={Settings:{Quality:2},GradeName:'high',ScreenExtent:[800,450],Scene:{DrawRuns:[]},Metrics:{Enabled:false,PassNames:['Clouds']}};
H.Telemetry=new SessionTelemetry(H);H.Telemetry.Event('csv-proof',{message:'comma, "quote" café'});H.Telemetry.Pause();
BuildDiagnostics(H,new EditorControls(()=>{}),document.getElementById('inspector'));window.fixture=H;
</script></body></html>`;
try{
 const Page=await Browser.newPage({acceptDownloads:true});const Errors=[];Page.on('pageerror',E=>Errors.push(String(E)));
 await Page.route('**/__csv-fixture__',R=>R.fulfill({contentType:'text/html',body:Fixture}));
 await Page.route('**/__csv-outer__',R=>R.fulfill({contentType:'text/html',body:'<iframe sandbox="allow-scripts allow-same-origin allow-downloads" src="/__csv-fixture__" style="width:100%;height:95vh"></iframe>'}));
 await Page.goto('http://127.0.0.1:8080/__csv-outer__');const Frame=Page.frameLocator('iframe');await Frame.getByRole('button',{name:'Download CSV',exact:true}).waitFor();
 const AwaitDownload=Page.waitForEvent('download');await Frame.getByRole('button',{name:'Download CSV',exact:true}).click();const Download=await AwaitDownload;
 assert.equal(Download.suggestedFilename(),'frontier-session.csv');assert.equal(await Download.failure(),null);const Data=await fs.readFile(await Download.path(),'utf8');assert.ok(Data.includes('csv-proof'));assert.ok(Data.includes('comma, ""quote"" café'));assert.ok(Data.includes('setting.Quality'));
 const Save=Frame.getByRole('link',{name:'Save CSV',exact:true});assert.match(await Save.getAttribute('href'),/^\/api\/telemetry\/exports\//);
 const Again=Page.waitForEvent('download');await Save.click();assert.equal(await (await Again).failure(),null);
 await Frame.getByRole('button',{name:'Show CSV / copy manually',exact:true}).click();const Text=Frame.getByRole('textbox',{name:'Complete session CSV'});assert.equal(await Text.inputValue(),Data.replace(/^\ufeff/,'').replaceAll('\r\n','\n'));
 assert.deepEqual(await Text.evaluate(E=>[E.selectionStart,E.selectionEnd]),[0,(await Text.inputValue()).length]);
 // Static hosting / unavailable attachment API must still produce a browser-file fallback.
 await Page.route('**/api/telemetry/exports',R=>R.fulfill({status:404}));
 const Fallback=Page.waitForEvent('download');await Frame.getByRole('button',{name:'Download CSV',exact:true}).click();assert.equal(await (await Fallback).failure(),null);assert.match(await Save.getAttribute('href'),/^blob:/);
 // Even when the enclosing iframe disallows downloads/popups, selected CSV remains usable.
 await Page.route('**/__csv-blocked__',R=>R.fulfill({contentType:'text/html',body:'<iframe sandbox="allow-scripts allow-same-origin" src="/__csv-fixture__"></iframe>'}));
 await Page.goto('http://127.0.0.1:8080/__csv-blocked__');await Frame.getByRole('button',{name:'Download CSV',exact:true}).click();await Frame.getByRole('link',{name:'Save CSV',exact:true}).waitFor();await Frame.getByRole('button',{name:'Show CSV / copy manually',exact:true}).click();assert.ok((await Frame.getByRole('textbox',{name:'Complete session CSV'}).inputValue()).includes('csv-proof'));
 assert.equal(Errors.length,0,Errors.join('\n'));
 console.log(JSON.stringify({iframeHttpAttachment:'passed',fileContents:'complete UTF-8 CSV',manualSaveLink:'passed',selectedTextFallback:'passed',blobFallback:'passed',sandboxBlockedCopyFallback:'passed',pageErrors:Errors},null,2));
}finally{await Browser.close();}
