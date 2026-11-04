// Render the pinned demo's own inspector builders and CSS without its scene renderer.
// Usage: node CaptureReference.mjs ReferenceRoot BrowserNodeModules OutputDirectory NativeContent
import {createServer} from 'node:http';
import {readFileSync, mkdirSync, writeFileSync, existsSync} from 'node:fs';
import {resolve, extname, join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [Root, Dependencies, Output, Content] = process.argv.slice(2).map(Path=>resolve(Path));
const {chromium: Playwright} = await import(pathToFileURL(join(Dependencies,'playwright-core/index.mjs')));
const {default: Chromium} = await import(pathToFileURL(join(Dependencies,'@sparticuz/chromium/build/cjs/index.cjs')));
const Styles = ['FrontierEditor','NativeEnvironment','SliderControl','CloudInspector','SunInspector','AtmosphereInspector',
    'CelestialInfographics','MoonCards','InspectorCategories','FogDashboard','LensOptics','WindInstruments'];
const Html = `<!doctype html><meta charset="utf-8">${Styles.map(Name=>`<link rel="stylesheet" href="Style/${Name}.css">`).join('')}
<style>html,body{margin:0;height:auto;overflow:visible}body{display:block;background:#101010;width:420px}#reference{width:420px;min-height:100vh;container-type:inline-size;overflow:visible}</style>
<div id="reference" class="native-environment"></div><script type="module">
import {NativePanelBuilders} from './Source/NativeEnvironmentPanels.js';
import {BuildWindField} from './Source/NativeWindPanel.js';
import {BuildLightningControls} from './Source/NativeLightningPanel.js';
import {EnvironmentDefaults} from './Source/EnvironmentSettings.js';
const Bytes=await (await fetch('Assets/Celestial/BrightStars.bin')).arrayBuffer();
const Floats=new Float32Array(Bytes,16); const Records=[];
for(let I=0;I<Floats.length;I+=7)Records.push(Array.from(Floats.slice(I,I+7)));
window.ConstructReference=(Name,Section=0)=>{
window.Current?.Disposers.forEach(F=>F());
const P=document.getElementById('reference');P.replaceChildren();P.className='native-environment';P.dataset.panel=Name;
const C={Bindings:[],Live:[],Disposers:[],Notify(){},Synchronise(){for(const F of [...this.Bindings])F();}};
const H={Settings:{...EnvironmentDefaults},StarCatalogue:{Records},CameraEye:[0,0,2],CameraState:{Forward:[1,0,0]},ScreenExtent:[1920,1080],
 Shell:{Select(){}},Metrics:{Enabled:false},Lightning:{Audio:{Enabled:false,Status:'Sound off'},Storm:{Bolts:[],Strikes:0}}};
window.Current=C;
if(Name==='wind')BuildWindField(H,C,P);else if(Name==='lightning')BuildLightningControls(H,C,P);else NativePanelBuilders[Name](H,C,P);
const Tabs=P.querySelectorAll('[role=tab],.inspector-category');if(Tabs[Section])Tabs[Section].click();
C.Synchronise();window.RefreshReference=()=>{for(const F of C.Live)F();};
};window.Ready=true;
</script>`;
const Moons = {luna:'MoonAlbedoSource2048',ember:'Ember2048',glacier:'Glacier2048',sulfur:'Sulfur2048',shroud:'Shroud2048',shard:'Shard2048'};
const Server = createServer((Request, Response)=>{
    try {
        const Path = decodeURIComponent(new URL(Request.url,'http://localhost').pathname);
        let Filename = resolve(Root,'.'+Path);
        if (!Filename.startsWith(Root+'/') && Filename!==Root) throw Error('Invalid path');
        if (Path==='/') {Response.setHeader('Content-Type','text/html');Response.end(Html);return;}
        if (Path==='/Assets/Celestial/BrightStars.bin') Filename=join(Content,'EnvironmentArchives/CelestialMaps/BrightStars.bin');
        const Match=Path.match(/^\/Assets\/Celestial\/(\w+)_2k.jpg$/);
        if(Match&&Moons[Match[1]])Filename=join(Content,'EnvironmentArchives/CelestialMaps',Moons[Match[1]]+'.jpg');
        Response.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css','.woff2':'font/woff2','.jpg':'image/jpeg'})[extname(Filename)]??'application/octet-stream');
        Response.end(readFileSync(Filename));
    }catch {Response.statusCode=404;Response.end();}
});
await new Promise(Resolve=>Server.listen(0,'127.0.0.1',Resolve));
mkdirSync(Output,{recursive:true});
const Browser=await Playwright.launch({executablePath:await Chromium.executablePath(),args:Chromium.args,headless:true});
try {
    const Page=await Browser.newPage({viewport:{width:420,height:1080},deviceScaleFactor:1});
    const Errors=[];Page.on('pageerror',Error=>Errors.push(String(Error)));
    await Page.goto('http://127.0.0.1:'+Server.address().port);
    await Page.waitForFunction(()=>window.Ready);
    const Captures=[];
    const Filter=process.argv[6]?.split(',');
    for(const [Name,Count] of [['clouds',4],['local-cloud',1],['wind',4],['fog',3],['aerial',3],['local-fog',1],['lightning',1],['stars',3],['moon',4],['flare',4]]) {
        if(Filter&&!Filter.includes(Name))continue;
        for(let Section=0;Section<Count;++Section) {
            try {
                await Page.evaluate(([Name,Section])=>window.ConstructReference(Name,Section),[Name,Section]);
                await Page.waitForTimeout(Name.includes('cloud')?6000:1000);
                await Page.evaluate(async()=>{await document.fonts.ready;window.RefreshReference();});
                await Page.waitForTimeout(350);
                const Filename=Name+'-'+Section+'.png';
                await Page.locator('#reference').screenshot({path:join(Output,Filename)});
                Captures.push(Filename);
            } catch(Error) {Errors.push(Name+': '+Error);}
        }
    }
    if(Filter&&existsSync(join(Output,'CaptureReport.json')))Captures.push(...JSON.parse(readFileSync(join(Output,'CaptureReport.json'))).captures.filter(Name=>!Captures.includes(Name)));
    writeFileSync(join(Output,'CaptureReport.json'),JSON.stringify({reference:'6afb6f00b5f25c45f7cda92c7da65b64845768f3',renderer:false,captures:Captures,errors:Errors},null,2));
    console.log(Captures.length+' demo captures; '+Errors.length+' errors');
}finally {await Browser.close();Server.close();}
