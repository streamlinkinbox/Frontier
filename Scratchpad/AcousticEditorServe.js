'use strict';
// ============================================================================================================================================
//                                                    ACOUSTICEDITORSERVE.JS
// ============================================================================================================================================
// 🧪 Minimal static server for testing Tools/AudioEditor/index.html in a browser session (no packages, no cache).
//    Run:  node Scratchpad/AcousticEditorServe.js [port]   → http://0.0.0.0:<port>/  serves the editor at /
//    Serves the repository read-only so the page, its TOMLs and the Scratchpad proof images are all reachable.
const fs = require('fs'), path = require('path'), http = require('http');

const repo = path.resolve(__dirname, '..');
const port = Number(process.argv[2] || 8080);
const types = { '.html': 'text/html; charset=utf-8', '.js': 'application/javascript; charset=utf-8', '.toml': 'text/plain; charset=utf-8',
                '.png': 'image/png', '.wav': 'audio/wav', '.log': 'text/plain; charset=utf-8', '.md': 'text/plain; charset=utf-8', '.css': 'text/css' };

http.createServer((req, res) =>
{
    let url = decodeURIComponent(req.url.split('?')[0]);
    if (url === '/' || url === '/index.html') url = '/Tools/AudioEditor/index.html';
    const file = path.normalize(path.join(repo, url));
    if (!file.startsWith(repo)) { res.writeHead(403); res.end(); return; }
    fs.readFile(file, (err, bytes) =>
    {
        if (err) { res.writeHead(404, { 'Content-Type': 'text/plain' }); res.end('not found: ' + url); return; }
        res.writeHead(200, { 'Content-Type': types[path.extname(file).toLowerCase()] || 'application/octet-stream', 'Cache-Control': 'no-store', 'Access-Control-Allow-Origin': '*' });
        res.end(bytes);
    });
}).listen(port, '0.0.0.0', () => console.log('AudioEditor at http://0.0.0.0:' + port + '/  (serving ' + repo + ')'));
