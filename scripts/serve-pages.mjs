// Strict static hosting for deployment tests: unlike Vite, missing files must
// return 404, never the SPA index. No source transforms or development globals.
import { createServer } from "node:http";
import { readFile, stat } from "node:fs/promises";
import { resolve, extname, relative, sep } from "node:path";
const args = process.argv.slice(2);
const root = resolve(args.includes("--source-root") ? "." : "docs");
const port = Number(args[args.indexOf("--port") + 1]) || 4173;
const mount = "/Frontier/";
const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".svg": "image/svg+xml",
  ".webp": "image/webp",
  ".woff": "font/woff",
  ".woff2": "font/woff2",
  ".txt": "text/plain; charset=utf-8",
};
createServer(async (req, res) => {
  try {
    const url = new URL(req.url, "http://static.invalid");
    if (url.pathname === "/Frontier") {
      res.writeHead(301, { Location: mount + url.search });
      res.end();
      return;
    }
    if (!url.pathname.startsWith(mount)) throw new Error("Not found");
    const path = decodeURIComponent(url.pathname.slice(mount.length));
    if (path.split("/").some((part) => part.startsWith(".")))
      throw new Error("Not found");
    let file = resolve(root, path);
    if (relative(root, file).split(sep).includes(".."))
      throw new Error("Not found");
    if ((await stat(file)).isDirectory()) file = resolve(file, "index.html");
    const body = await readFile(file);
    res.writeHead(200, {
      "Content-Type": types[extname(file)] || "application/octet-stream",
      "Cache-Control": "no-store",
    });
    res.end(req.method === "HEAD" ? undefined : body);
  } catch {
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not found");
  }
}).listen(port, "0.0.0.0", () =>
  console.log(`Static Pages test: http://localhost:${port}${mount}`),
);
