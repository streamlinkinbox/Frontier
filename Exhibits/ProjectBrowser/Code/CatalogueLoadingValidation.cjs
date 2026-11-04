//============================================================================================================================================
//                                                     CATALOGUELOADINGVALIDATION.CJS
//============================================================================================================================================
// 🧩 Verify cache-separated prototype assets and visible catalogue entries at the embedded preview width.
const Fs = require('node:fs');
const Path = require('node:path');
const Assert = require('node:assert/strict');
const Crypto = require('node:crypto');
const ChromiumPackage = process.env.CHROMIUM_PATH ? null : require('@sparticuz/chromium');
const {chromium} = require('playwright-core');
const Destination = Path.resolve(__dirname, '..');
async function Main()
{
    if (ChromiumPackage)
        await ChromiumPackage.inflate(Path.resolve(Path.dirname(require.resolve('@sparticuz/chromium')), '../bin/al2023.tar.br'));
    const Browser = await chromium.launch({executablePath:process.env.CHROMIUM_PATH || await ChromiumPackage.default.executablePath(),
        args:ChromiumPackage ? [...ChromiumPackage.default.args,'--enable-unsafe-swiftshader'] :
            ['--no-sandbox','--disable-dev-shm-usage','--use-angle=swiftshader','--enable-unsafe-swiftshader'],headless:true,
        env:{...process.env,LD_LIBRARY_PATH:process.env.CHROMIUM_LIBRARY_PATH || '/tmp/al2023/lib:/tmp:'+(process.env.LD_LIBRARY_PATH || '')}});
    try
    {
        const Page = await Browser.newPage({viewport:{width:890,height:800}});
        Page.setDefaultTimeout(5000);
        const Errors = [], Requests = [];
        Page.on('pageerror',Failure=>Errors.push(Failure.message));
        Page.on('request',Request=>Requests.push(Request.url()));
        // An old unversioned script must never be selected by the corrected document.
        await Page.route('**/Browser.js',Route=>Route.fulfill({contentType:'text/javascript',body:'throw new Error("Old cached script selected");'}));
        await Page.goto(process.env.PROJECT_BROWSER_URL || 'http://127.0.0.1:8770',{waitUntil:'networkidle'});
        for(const Name of ['Styles.css','PanelLayout.css','ReferenceRenderer.js','Browser.js'])
        {
            const Digest=Crypto.createHash('sha256').update(Fs.readFileSync(Path.resolve('Experimental/Project browser',Name))).digest('hex').slice(0,12);
            Assert(Requests.some(Address=>Address.endsWith('/'+Name+'?v='+Digest)),Name+' must request its current content revision');
        }
        await Page.waitForFunction(()=>document.querySelectorAll('#Cards img[src^="data:image/png"]').length===6);
        Assert.equal(await Page.locator('#Cards .file-card').count(),6);
        Assert.equal(await Page.locator('#CollectionCount').textContent(),'6 sample files');
        const VisibleCards=await Page.locator('#Cards .file-card').evaluateAll(Cards=>Cards.filter(Card=>{
            const Rectangle=Card.getBoundingClientRect();
            return Rectangle.width>0 && Rectangle.height>0 && Rectangle.top<innerHeight && Rectangle.bottom>0;
        }).length);
        Assert(VisibleCards>0,'Catalogue must visibly contain file cards, not only a count');
        await Page.screenshot({path:Path.join(Destination,'Images/CatalogueEmbedded.png')});
        for(const Token of ['valley','mount','stone','island','bracket','faceted'])
        {
            await Page.locator('#Cards [data-open="'+Token+'"]').first().click();
            await Assert.doesNotReject(()=>Page.locator('#Reader').waitFor({state:'visible'}));
            await Page.locator('#Back').click();
            Assert.equal(await Page.locator('#Cards .file-card').count(),6);
        }
        await Page.locator('[data-filter="cad"]').click();
        Assert.equal(await Page.locator('#Cards .file-card').count(),2);
        await Page.locator('[data-filter="all"]').click();
        await Page.reload({waitUntil:'networkidle'});
        Assert.equal(await Page.locator('#Cards .file-card').count(),6);
        Assert.deepEqual(Errors,[]);
        const Report={result:'passed',viewport:{width:890,height:800},visible_cards:VisibleCards,total_cards:6,
            checks:['Content-hashed asset URLs bypass old unversioned scripts','Six rendered file cards and visible entries at embedded preview size',
                'All six files open and return to the catalogue','CAD filtering and reload preserve visible catalogue'],page_errors:Errors};
        Fs.writeFileSync(Path.join(Destination,'Results/CatalogueLoadingChecks.json'),JSON.stringify(Report,null,2)+'\n');
        console.log(JSON.stringify(Report,null,2));
    }
    finally {await Browser.close();}
}
Main().catch(Failure=>{console.error(Failure);process.exitCode=1;});
