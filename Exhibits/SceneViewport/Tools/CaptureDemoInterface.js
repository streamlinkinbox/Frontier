/*==========================================================================================================================================
                                                       CAPTUREDEMOINTERFACE.JS
==========================================================================================================================================*/
// 🧩 Reference captures of the VehicleHMI demo's interface for the 1:1 port: the shell once start-up finishes, its regions'
//    HTML, then every Outliner entry's Inspector page. Headless Chromium on SwiftShader through puppeteer-core.
//
// 📝 Setup (Linux, see Exhibits/SceneViewport/InterfacePortInventory.md):
//    ① the demo at SultanAladin/Frontier 6afb6f00 with ReferenceApps/Working/{Precipitation,ParticleEditor}, served from that
//      checkout's root (python3 -m http.server 8765 --bind 127.0.0.1);
//    ② Chromium from npm @sparticuz/chromium 153.0.0 (the tarball Automation/PrepareVulkanCompute.py fetches for SwiftShader):
//      bin/chromium.br, al2023.tar.br, fonts.tar.br and swiftshader.tar.br brotli-decoded into one folder;
//    ③ npm install puppeteer-core@23 (PUPPETEER_SKIP_DOWNLOAD=1).
//    FRONTIER_DEMO_CHROMIUM = that folder; FRONTIER_DEMO_OUTPUT = where the captures go; FRONTIER_DEMO_URL overrides the page.
//    The demo builds its editor only after its GPU bring-up; quality=low, scale=0.5, stresspool=0, capture=1 and omit=Cabin
//    keep that within reach of a CPU adapter (about 50 s).

const puppeteer = require('puppeteer-core');
const fs = require('fs');
const path = require('path');

const ChromiumRoot = process.env.FRONTIER_DEMO_CHROMIUM;
const Output = process.env.FRONTIER_DEMO_OUTPUT || path.join(process.cwd(), 'DemoInterface');
const Address = process.env.FRONTIER_DEMO_URL ||
    'http://127.0.0.1:8765/Experimental/VehicleHmiPrototype/index.html?quality=low&scale=0.5&stresspool=0&capture=1&omit=Cabin';
const LimitSeconds = Number(process.argv[2] || 600);
const RowLimit = Number(process.argv[3] || 48);

fs.mkdirSync(Output, { recursive: true });
const Journal = [];
const Note = (Line) => { Journal.push(Line); fs.writeFileSync(path.join(Output, 'Journal.txt'), Journal.join('\n') + '\n'); };
const Pause = (Seconds) => new Promise((Resolve) => setTimeout(Resolve, Seconds * 1000));

(async () => {
    if (!ChromiumRoot) throw new Error('FRONTIER_DEMO_CHROMIUM is not set');
    // 📝 The serverless fonts.conf looks in /var/task and /tmp; this one points at the bundled Open Sans and the engine's fonts.
    const FontConfiguration = path.join(ChromiumRoot, 'local-fonts.conf');
    fs.writeFileSync(FontConfiguration, `<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>${path.join(ChromiumRoot, 'fonts/fonts')}</dir>
  <dir>${path.resolve(__dirname, '../../../EngineContent/FontArchives')}</dir>
  <cachedir>${path.join(ChromiumRoot, 'fonts-cache')}</cachedir>
</fontconfig>
`);
    const Browser = await puppeteer.launch({
        executablePath: path.join(ChromiumRoot, 'chromium'),
        headless: 'new',
        protocolTimeout: 900000,
        env: { ...process.env, FONTCONFIG_FILE: FontConfiguration, LD_LIBRARY_PATH: path.join(ChromiumRoot, 'lib/lib') + ':' + ChromiumRoot },
        args: ['--no-sandbox', '--disable-dev-shm-usage', '--hide-scrollbars', '--window-size=1920,1080', '--enable-unsafe-webgpu',
               '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist',
               '--disable-gpu-watchdog', '--disable-background-timer-throttling', '--disable-renderer-backgrounding'],
        defaultViewport: { width: 1920, height: 1080, deviceScaleFactor: 1 },
    });
    const Page = await Browser.newPage();
    Page.on('console', (Message) => Note('[console] ' + Message.text().slice(0, 300)));
    Page.on('pageerror', (Failure) => Note('[pageerror] ' + Failure.message.slice(0, 300)));
    await Page.goto(Address, { waitUntil: 'domcontentloaded', timeout: 120000 });

    // ① start-up: the bring-up overlay hides once the editor is built
    const Start = Date.now();
    let Ready = false;
    while ((Date.now() - Start) / 1000 < LimitSeconds)
    {
        await Pause(10);
        const Status = await Page.evaluate(() => ({
            Hidden: document.getElementById('bring-up')?.classList.contains('hidden'),
            Caption: document.querySelector('.bring-up-caption')?.textContent,
        }));
        Note(`[wait] ${((Date.now() - Start) / 1000).toFixed(0)} s ${JSON.stringify(Status)}`);
        if (Status.Hidden) { Ready = true; break; }
    }
    await Pause(8);
    await Page.screenshot({ path: path.join(Output, 'Shell.png') });
    const Regions = await Page.evaluate(() => {
        const Describe = {};
        for (const Selector of ['.viewport-tabs', '#viewport-bar', '#hud', '#status', '#inspector-top', '#inspector-content', '#outliner-body',
                                '#overlay-dock', '#overlay-notice', '#event-notice', '#bring-up', '#failure', '#inset-frame'])
            Describe[Selector] = document.querySelector(Selector)?.outerHTML ?? null;
        return Describe;
    });
    fs.writeFileSync(path.join(Output, 'Regions.json'), JSON.stringify(Regions, null, 2));
    Note('[ready] ' + Ready);

    // ② every Outliner entry once, and the Inspector page it opens
    if (Ready)
    {
        const Entries = await Page.$$('#outliner-body .object-button');
        Note('[entries] ' + Entries.length);
        for (let Index = 0; Index < Math.min(Entries.length, RowLimit); ++Index)
        {
            const Name = (await Entries[Index].evaluate((Element) => (Element.innerText || '').trim().split('\n')[0])).replace(/[^A-Za-z0-9]+/g, '-').slice(0, 40);
            const Stem = `Page-${String(Index).padStart(2, '0')}-${Name}`;
            await Entries[Index].click();
            await Pause(1.5);
            await Page.screenshot({ path: path.join(Output, Stem + '.png') });
            fs.writeFileSync(path.join(Output, Stem + '.html'), await Page.evaluate(() => document.querySelector('#inspector-content')?.outerHTML ?? ''));
        }
    }
    await Browser.close();
})().catch((Failure) => { Note('[fatal] ' + ((Failure && Failure.stack) || Failure)); process.exit(1); });
