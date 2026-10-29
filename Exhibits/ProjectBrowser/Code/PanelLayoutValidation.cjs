//============================================================================================================================================
//                                                       PANELLAYOUTVALIDATION.CJS
//============================================================================================================================================
// 🧩 Verify fixed column boundaries, aligned headers, A4 flow, pill versions and reversible panel collapse in Chromium.
const Fs = require('node:fs');
const Path = require('node:path');
const Assert = require('node:assert/strict');
const Crypto = require('node:crypto');
const ChromiumPackage = require('@sparticuz/chromium');
const Chromium = ChromiumPackage.default;
const {chromium} = require('playwright-core');
const Destination = Path.resolve(__dirname, '..');
async function Main()
{
    await ChromiumPackage.inflate(Path.resolve(Path.dirname(require.resolve('@sparticuz/chromium')), '../bin/al2023.tar.br'));
    const Browser = await chromium.launch({executablePath:await Chromium.executablePath(),args:[...Chromium.args,'--enable-unsafe-swiftshader'],headless:true,
        env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib:/tmp:'+(process.env.LD_LIBRARY_PATH || '')}});
    const Errors = [], Checks = [];
    try
    {
        const Page = await Browser.newPage({viewport:{width:1440,height:1000}});
        Page.setDefaultTimeout(5000);
        Page.on('pageerror',Failure=>Errors.push(Failure.message));
        await Page.goto(process.env.PROJECT_BROWSER_URL || 'http://127.0.0.1:8770',{waitUntil:'networkidle'});
        await Page.waitForFunction(()=>document.querySelectorAll('#Cards img[src^="data:image/png"]').length===6);
        Assert.equal(await Page.locator('.titlebar').count(),0);
        Assert.equal(await Page.locator('.preview-caption-tag').count(),0);
        const Inspect = Selector => Page.locator(Selector).evaluate(Surface=>{
            const Rectangle=Surface.getBoundingClientRect();return {x:Rectangle.x,y:Rectangle.y,width:Rectangle.width,height:Rectangle.height};
        });
        const LibraryColumns=await Promise.all(['.library-rail','.library-main','.library-inspector'].map(Inspect));
        const LibraryHeaders=await Page.locator('#Library .pane-header').evaluateAll(Headers=>Headers.map(Header=>{const Rectangle=Header.getBoundingClientRect();return {y:Rectangle.y,height:Rectangle.height};}));
        Assert.deepEqual(LibraryHeaders,[{y:0,height:64},{y:0,height:64},{y:0,height:64}]);
        await Page.screenshot({path:Path.join(Destination,'Images/AlignedLibrary.png')});
        await Page.locator('[data-open="valley"]').first().click();
        await Page.waitForFunction(()=>document.querySelector('.render-canvas')?.width>300);
        const ReaderColumns=await Promise.all(['.preview-rail','.preview-main','.inspector'].map(Inspect));
        Assert.deepEqual(ReaderColumns,LibraryColumns);
        const ReaderHeaders=await Page.locator('#Reader .pane-header').evaluateAll(Headers=>Headers.map(Header=>{const Rectangle=Header.getBoundingClientRect();return {y:Rectangle.y,height:Rectangle.height};}));
        Assert.deepEqual(ReaderHeaders,LibraryHeaders);
        Checks.push('Three identical library/reader column boundaries and three equal headers at y=0; branding header removed');
        const Pill=await Page.locator('#RevisionSelect').evaluate(Control=>({Radius:getComputedStyle(Control).borderRadius,Height:Control.getBoundingClientRect().height}));
        Assert(parseFloat(Pill.Radius)>=Pill.Height/2);
        await Page.locator('#RevisionSelect').selectOption('2');
        Assert.equal(await Page.locator('#RevisionSelect').inputValue(),'2');
        Checks.push('Working selectable pill-shaped version control');
        await Page.locator('#Document').click();
        const Paper=await Page.locator('.document-page').evaluate(Paper=>{
            const Rectangle=Paper.getBoundingClientRect(),Canvas=Paper.parentElement.getBoundingClientRect();
            return {width:Rectangle.width,height:Rectangle.height,canvasWidth:Canvas.width,scrollHeight:Paper.scrollHeight,clientHeight:Paper.clientHeight,
                overflow:getComputedStyle(Paper).overflow,canvasOverflow:getComputedStyle(Paper.parentElement).overflow,
                imageCount:Paper.querySelectorAll('img').length};
        });
        Assert(Math.abs(Paper.height/Paper.width-297/210)<.01,'Overview must retain A4 proportions');
        Assert(Paper.width<=Paper.canvasWidth+1,'Whole sheet fits viewer width');
        Assert(Paper.scrollHeight<=Paper.clientHeight+1,'Document does not overflow/cut its sheet');
        Assert.equal(Paper.canvasOverflow,'visible');
        const PaperColumns=await Promise.all(['.preview-rail','.preview-main','.inspector'].map(Inspect));
        Assert.deepEqual(PaperColumns,ReaderColumns);
        await Page.screenshot({path:Path.join(Destination,'Images/A4Overview.png')});
        await Page.locator('.viewer-scroll').evaluate(Surface=>Surface.scrollTop=Surface.scrollHeight);
        const BottomReachable=await Page.locator('.document-page').evaluate(Paper=>Paper.getBoundingClientRect().bottom<=Paper.closest('.viewer-scroll').getBoundingClientRect().bottom);
        Assert(BottomReachable,'Bottom of A4 document must be reachable by scrolling');
        Checks.push('A4 aspect ratio, whole-sheet fit, no internal clipping, bottom reachable; document does not alter panel widths');
        console.log('A4 assertions passed; testing collapse');
        await Page.locator('#Reader [data-collapse="left"]').click();
        Assert.equal((await Inspect('.preview-rail')).width,44);
        await Page.locator('#Reader [data-collapse="right"]').click();
        Assert.equal((await Inspect('.inspector')).width,44);
        await Page.locator('#Back').click();
        Assert.equal((await Inspect('.library-rail')).width,44);
        Assert.equal((await Inspect('.library-inspector')).width,44);
        await Page.locator('#Library [data-collapse="left"]').click();
        await Page.locator('#Library [data-collapse="right"]').click();
        Assert.deepEqual(await Promise.all(['.library-rail','.library-main','.library-inspector'].map(Inspect)),LibraryColumns);
        Checks.push('Independent collapse to 44px, expand and persistence between catalogue and reader; back remains accessible');
        await Page.setViewportSize({width:390,height:844});
        Assert(await Page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
        await Page.locator('[data-open="mount"]').first().click();
        await Page.locator('#Reader [data-collapse="left"]').click();
        await Page.locator('#Document').click();
        Assert(await Page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
        await Page.screenshot({path:Path.join(Destination,'Images/A4Narrow.png')});
        Checks.push('390px catalogue and document reader fit viewport with compact collapsed side panels');
        Assert.deepEqual(Errors,[]);
        Fs.writeFileSync(Path.join(Destination,'Results/PanelLayoutChecks.json'),JSON.stringify({result:'passed',browser:await Browser.version(),checks:Checks,
            columns:ReaderColumns,headers:ReaderHeaders,paper:Paper,page_errors:Errors,
            source_sha256:Object.fromEntries(['index.html','Styles.css','PanelLayout.css','Browser.js','ReferenceRenderer.js'].map(Name=>[Name,Crypto.createHash('sha256').update(Fs.readFileSync(Path.resolve('Experimental/Project browser',Name))).digest('hex')]))},null,2)+'\n');
        console.log('PASS: '+Checks.join('; '));
    }
    finally {await Browser.close();}
}
Main().catch(Failure=>{console.error(Failure);process.exitCode=1;});
