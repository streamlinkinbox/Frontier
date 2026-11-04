//============================================================================================================================================
//                                                      VERIFYNATIVEEVIDENCEPAGE.CJS
//============================================================================================================================================
// 🧩 Verify the earlier native-review page displays the exact current GIF while retaining its original evidence navigation.

const Assert = require('node:assert/strict');
const Crypto = require('node:crypto');
const Fs = require('node:fs');
const Path = require('node:path');
const Puppeteer = require('puppeteer-core');
const Chromium = require('@sparticuz/chromium');
const Root = Path.resolve(__dirname, '../../..');
const Digest = Bytes => Crypto.createHash('sha256').update(Bytes).digest('hex');
const CaptureCount = JSON.parse(Fs.readFileSync(Path.join(Root, 'Exhibits/SpaceFormats/Results/SourceAudit.json'))).images.length;
const Url = process.argv[2] || 'http://127.0.0.1:8765';

(async () =>
{
    const Browser = await Puppeteer.launch({executablePath: await Chromium.executablePath(), args: Chromium.args, headless: true});
    try
    {
        const Page = await Browser.newPage();
        await Page.setViewport({width: 1440, height: 1000});
        const Errors = [];
        Page.on('pageerror', Error => Errors.push(Error.message));
        const Response = await Page.goto(Url + '/#PatternNativeMotion', {waitUntil: 'networkidle0'});
        Assert.equal(Response.status(), 200);
        const Images = await Page.$$eval('#PatternNativeMotion img', Images => Images.map(Image =>
            ({uri: Image.src, width: Image.naturalWidth, height: Image.naturalHeight, complete: Image.complete})));
        const Evidence = JSON.parse(Fs.readFileSync(Path.join(Root, 'Exhibits/TextureStudio/Results/NativePatternEvidencePageChecks.json')));
        const Captions = [Evidence.current_caption, ...Evidence.historical_captions];
        Assert.equal(Images.length, Captions.length);
        for (let Index = 0; Index < Images.length; ++Index)
        {
            const Image = Images[Index];
            Assert(Image.uri.startsWith('data:image/gif;base64,'));
            Assert(Image.complete && Image.width === 1440 && Image.height === 900);
            Assert.equal(Digest(Buffer.from(Image.uri.split(',')[1], 'base64')),
                Digest(Fs.readFileSync(Path.join(Root, 'Exhibits/TextureStudio/Images', Captions[Index] + '.gif'))));
        }
        const Tabs = await Page.$$eval('[data-tab]', Tabs => Tabs.map(Tab => Tab.dataset.tab));
        Assert.equal(Tabs.length, 5);
        for (const Tab of Tabs)
        {
            await Page.click('[data-tab="' + Tab + '"]');
            Assert.equal(await Page.$eval('#' + Tab, Section => Section.hidden), false);
        }
        await Page.click('[data-tab="evidence"]');
        Assert.equal(await Page.$$eval('#evidence [data-image]', Captures => Captures.length), CaptureCount);
        await Page.click('#PatternNativeMotion details summary');
        Assert.equal(await Page.$eval('#PatternNativeMotion details', Details => Details.open), true);
        Assert.deepEqual(Errors, []);
        for (const Route of ['/.git/config', '/Exhibits/../../.git/config'])
            Assert.equal((await Page.goto(Url + Route)).status(), 404);
        const Report = {result: 'passed', gif_images_decoded_and_hashed: Images.length, original_tabs_exercised: Tabs.length,
            original_capture_choices_preserved: CaptureCount, private_paths_refused: true, pixel_stream: false,
            viewer_sha256: Digest(Fs.readFileSync(Path.join(Root, 'Exhibits/SpaceFormats/index.html'))),
            source_sha256: {'Exhibits/TextureStudio/Code/VerifyNativeEvidencePage.cjs': Digest(Fs.readFileSync(__filename))},
            limitations: 'Browser evidence-display checks only; native authoring execution is validated separately.'};
        Fs.writeFileSync(Path.join(Root, 'Exhibits/TextureStudio/Results/NativePatternEvidenceBrowserChecks.json'),
            JSON.stringify(Report, null, 2) + '\n');
        console.log('PASS: current and historical GIF bytes decoded; earlier tabs and ' + CaptureCount + ' capture choices retained; private paths refused.');
    }
    finally
    {
        await Browser.close();
    }
})().catch(Error => {console.error(Error); process.exitCode = 1;});
