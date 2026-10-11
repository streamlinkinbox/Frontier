// Minimal static dev server (no dependencies). Serves the repository root on 0.0.0.0.
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const port = +(process.env.PORT || process.argv[2] || 8080);
const types = { ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8", ".json": "application/json", ".png": "image/png", ".svg": "image/svg+xml", ".wasm": "application/wasm", ".obj": "text/plain", ".md": "text/plain; charset=utf-8" };

http.createServer((req, res) => {
  let url = decodeURIComponent(req.url.split("?")[0]);
  // the app lives in /RoadGenerator/ and uses relative asset URLs — redirect the root so they resolve
  if (url === "/" || url === "/index.html") { res.writeHead(302, { Location: "/RoadGenerator/" }); return res.end(); }
  let file = path.join(root, url);
  if (!file.startsWith(root)) { res.writeHead(403); return res.end(); }
  // fallback: root-relative requests for app files (e.g. /src/ui/app.js) map into RoadGenerator/
  if (!fs.existsSync(file) && fs.existsSync(path.join(root, "RoadGenerator", url))) file = path.join(root, "RoadGenerator", url);
  fs.stat(file, (err, st) => {
    if (err) { res.writeHead(404); return res.end("not found"); }
    const target = st.isDirectory() ? path.join(file, "index.html") : file;
    fs.readFile(target, (e2, data) => {
      if (e2) { res.writeHead(404); return res.end("not found"); }
      res.writeHead(200, { "Content-Type": types[path.extname(target)] || "application/octet-stream", "Cache-Control": "no-store", "Access-Control-Allow-Origin": "*" });
      res.end(data);
    });
  });
}).listen(port, "0.0.0.0", () => console.log(`frontier. roads → http://0.0.0.0:${port}/  (app at /RoadGenerator/index.html)`));
