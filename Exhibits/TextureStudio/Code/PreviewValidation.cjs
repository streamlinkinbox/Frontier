//============================================================================================================================================
//                                                          PREVIEWVALIDATION.CJS
//============================================================================================================================================
// 🧩 Verify the correct pinned TextureStudio application opens, renders its teapot and switches wireframe modes.

const Fs = require('node:fs');
const Path = require('node:path');
const Assert = require('node:assert/strict');
const Package = require('@sparticuz/chromium');
const {chromium} = require('playwright-core');
async function Main()
{
    await Package.inflate(Path.resolve(Path.dirname(require.resolve('@sparticuz/chromium')),'../bin/al2023.tar.br'));
    const Browser = await chromium.launch({executablePath:await Package.default.executablePath(),
        args:[...Package.default.args,'--enable-unsafe-swiftshader'],headless:true,
        env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib:/tmp:'+(process.env.LD_LIBRARY_PATH||'')}});
    try
    {
        const Page = await Browser.newPage({viewport:{width:1440,height:1000}});
        Page.setDefaultTimeout(20000);
        const Errors = [], External = [];
        Page.on('pageerror',Failure=>Errors.push(Failure.message));
        Page.on('request',Request=>{
            if (/^https?:/.test(Request.url()) && new URL(Request.url()).hostname !== '127.0.0.1') External.push(Request.url());
        });
        await Page.goto('http://127.0.0.1:8774/',{waitUntil:'domcontentloaded'});
        const Canvas = Page.locator('.tp-studio canvas');
        await Page.waitForFunction(()=>document.querySelector('.tp-studio canvas')?.dataset.materialReady==='true');
        Assert.equal(await Canvas.getAttribute('data-preview-shape'),'Teapot');
        const Triangles = Number(await Canvas.getAttribute('data-triangle-count'));
        Assert(Triangles>10000);
        await Page.getByRole('button',{name:'Wireframe',exact:true}).click();
        Assert.equal(await Canvas.getAttribute('data-wireframe'),'true');
        await Page.getByRole('button',{name:'Lit',exact:true}).click();
        Assert.equal(await Canvas.getAttribute('data-wireframe'),'false');
        const Destination=Path.resolve(__dirname,'..');
        Fs.mkdirSync(Path.join(Destination,'Images'),{recursive:true});
        await Page.screenshot({path:Path.join(Destination,'Images/TextureStudio.png')});
        Assert.deepEqual(Errors,[]);
        Assert.deepEqual(External,[]);
        const Report={result:'passed',source:'streamlinkinbox/Frontier',commit:'773b2f5a2672ae530accff000d65d779e9059b66',
            checks:['Texture workspace opens from root redirect','Real teapot geometry and ready renderer','Wireframe/Lit switching','No runtime external HTTP requests'],
            triangle_count:Triangles,page_errors:Errors,external_requests:External,execution:'Chromium software WebGL; not native Vulkan or physical GPU validation'};
        Fs.writeFileSync(Path.join(Destination,'Results/PreviewChecks.json'),JSON.stringify(Report,null,2)+'\n');
        console.log(JSON.stringify(Report,null,2));
    }
    finally {await Browser.close();}
}
Main().catch(Failure=>{console.error(Failure);process.exitCode=1;});
