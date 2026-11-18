// Trusted local proof scene only. Run with a temporary lab and an explicit Chromium executable.
const fs = require("node:fs"),
  path = require("node:path"),
  http = require("node:http"),
  zlib = require("node:zlib");
const puppeteer = require("puppeteer-core");
async function main() {
  const [rootArg, outArg, jobsFile] = process.argv.slice(2);
  if (!jobsFile || !process.env.CHROME)
    throw Error("CHROME=... node capture.cjs Experimental output jobs.json|pt");
  const root = path.resolve(rootArg),
    out = path.resolve(outArg);
  fs.mkdirSync(out, { recursive: true });
  const server = http.createServer((req, res) => {
    let file = path.resolve(
      root,
      "." + new URL(req.url, "http://localhost").pathname,
    );
    if (!file.startsWith(root + path.sep)) {
      res.writeHead(403);
      return res.end();
    }
    if (fs.existsSync(file) && fs.statSync(file).isDirectory())
      file = path.join(file, "index.html");
    if (!fs.existsSync(file)) {
      res.writeHead(404);
      return res.end();
    }
    res.setHeader(
      "Content-Type",
      { ".js": "text/javascript", ".html": "text/html", ".css": "text/css" }[
        path.extname(file)
      ] || "application/octet-stream",
    );
    fs.createReadStream(file).pipe(res);
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  let browser;
  try {
    browser = await puppeteer.launch({
      executablePath: process.env.CHROME,
      headless: true,
      args: JSON.parse(process.env.CHROME_ARGS || "[]"),
      protocolTimeout: 600000,
    });
    const page = await browser.newPage();
    page.on("pageerror", console.error);
    const url = "http://127.0.0.1:" + server.address().port;
    if (jobsFile === "pt") {
      await page.goto(
        url +
          "/DeterministicPathTracer/?validation=1&width=480&height=270&denoise=0&trace=1&present=readback&integrator=megakernel",
      );
      await page.waitForFunction(() => document.body.dataset.ready);
      for (const n of [1, 16, 64]) {
        const r = await page.evaluate(async (n) => {
          const t = window.DeterministicTracing;
          await t.ContinueSamples(n);
          const sum = await t.RetrieveAccumulation();
          if (!sum.every(Number.isFinite)) throw Error("nonfinite PT sum");
          return {
            png: await t.EncodeDisplayPng(),
            hash: await t.ComputeDigest(),
          };
        }, n);
        fs.writeFileSync(
          path.join(out, `pt${n}.png`),
          Buffer.from(r.png.split(",")[1], "base64"),
        );
        console.log(n, r.hash);
      }
      return;
    }
    let folder;
    await page.exposeFunction("saveSnapshot", (name, data) => {
      if (!/^[A-Za-z0-9_.]+$/.test(name)) throw Error("snapshot name");
      fs.mkdirSync(folder, { recursive: true });
      fs.writeFileSync(
        path.join(folder, name),
        zlib.gunzipSync(Buffer.from(data, "base64")),
      );
    });
    await page.goto(url + "/ShadowAlgorithmLab/");
    await page.waitForFunction(() => typeof window.capture === "function");
    for (const [id, params, plane] of JSON.parse(
      fs.readFileSync(jobsFile, "utf8"),
    )) {
      if (!/^[A-Za-z0-9_-]+$/.test(id)) throw Error("job name");
      folder = path.join(out, "native", id);
      const r = await page.evaluate(
        (p, a) => window.capture(p, a),
        params,
        plane,
      );
      if (r.error) throw Error(`GL ${r.error}`);
      fs.writeFileSync(
        path.join(out, id + (plane ? ".raw" : ".png")),
        Buffer.from(r.data || r.png, "base64"),
      );
      console.log(id, r.ms);
    }
  } finally {
    if (browser) await browser.close();
    server.close();
  }
}
main().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
