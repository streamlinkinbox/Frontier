//============================================================================================================================================
//                                                        REVIEWVALIDATION.CJS
//============================================================================================================================================
// 🧩 Exercise the actual evidence viewer DOM and verify embedded source/image bytes without a new native-render claim.

const Fs = require('node:fs');
const Path = require('node:path');
const Assert = require('node:assert/strict');
const Crypto = require('node:crypto');
const { JSDOM, VirtualConsole } = require('jsdom');

const Exhibit = Path.resolve(__dirname, '..');
const Audit = JSON.parse(Fs.readFileSync(Path.join(Exhibit, 'Results/SourceAudit.json'), 'utf8'));
const Errors = [];
const ConsoleOutput = new VirtualConsole();
ConsoleOutput.on('jsdomError', Error => Errors.push(Error.message));
const Dom = new JSDOM(Fs.readFileSync(Path.join(Exhibit, 'index.html'), 'utf8'), {
    runScripts: 'dangerously', virtualConsole: ConsoleOutput, url: 'https://review.example/'
});
const Document = Dom.window.document;
const Digest = Bytes => Crypto.createHash('sha256').update(Bytes).digest('hex');
const Decode = URI => Buffer.from(URI.slice(URI.indexOf(',') + 1), 'base64');
const Tabs = [...Document.querySelectorAll('[data-tab]')];
Assert.equal(Tabs.length, 5);
for (const Tab of Tabs) {
    Tab.click();
    Assert.equal(Tab.getAttribute('aria-selected'), 'true');
    Assert.equal(Document.getElementById(Tab.dataset.tab).hidden, false);
    Assert.equal(Document.querySelectorAll('[role="tabpanel"]:not([hidden])').length, 1);
}
Document.querySelector('[data-tab="evidence"]').click();
const Captures = [...Document.querySelectorAll('[data-image]')];
Assert.equal(Captures.length, Audit.images.length);
for (let Index = 0; Index < Captures.length; ++Index) {
    Captures[Index].click();
    const Preview = Document.getElementById('EvidencePreview');
    Assert.equal(Digest(Decode(Preview.src)), Audit.images[Index].sha256);
    Assert.equal(Preview.alt, Audit.images[Index].label);
    Assert.equal(Document.getElementById('EvidenceTitle').textContent, Audit.images[Index].label);
    Assert.equal(Document.querySelectorAll('[data-image][aria-pressed="true"]').length, 1);
}
Assert.equal(Document.querySelectorAll('#audit tbody tr').length, Audit.findings.length);
Assert.equal(Document.querySelectorAll('#pins tbody tr').length, Audit.sources.length + Audit.navigation_reference.sources.length);
const Original = Document.querySelector('[download="SpaceFormatsOriginalBrief.txt"]');
const Plan = Document.querySelector('[download="SpaceFormats.md"]');
Assert.equal(Digest(Decode(Original.href)), Audit.original_brief_sha256);
Assert.equal(Digest(Decode(Plan.href)), Audit.refined_plan_sha256);
for (const Link of Document.querySelectorAll('a[target="_blank"]')) {
    Assert(Link.href.startsWith('https://github.com/SultanAladin/Frontier-/blob/' + Audit.baseline_commit + '/'));
    Assert.equal(Link.rel, 'noopener');
}
Assert(Document.body.textContent.includes('B approved'));
Assert(Document.body.textContent.includes('Editor/Authoring'));
Assert.equal(Document.querySelectorAll('script[src],link[rel="stylesheet"]').length, 0);
Assert.deepEqual(Errors, []);
const Result = {
    result: 'passed', checkout_revision: Audit.review_checkout_revision,
    viewer_sha256: Digest(Fs.readFileSync(Path.join(Exhibit, 'index.html'))),
    tabs_exercised: Tabs.length, captures_selected_and_hashed: Captures.length,
    audit_rows: Audit.findings.length, pinned_source_rows: Audit.sources.length, ui_reference_rows: Audit.navigation_reference.sources.length,
    original_and_plan_download_hashes: 'passed', external_frontend_dependencies: 0,
    jsdom_version: require('jsdom/package.json').version,
    limitations: 'DOM interaction only; no full-browser visual run or proposed-codec acceptance; native UI checks are recorded separately.'
};
Fs.writeFileSync(Path.join(Exhibit, 'Results/ViewerChecks.json'), JSON.stringify(Result, null, 2) + '\n');
console.log(JSON.stringify(Result, null, 2));
Dom.window.close();
