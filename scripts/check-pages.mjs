import assert from "node:assert/strict";
import { readFile, readdir, stat } from "node:fs/promises";
import { dirname, resolve, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";

export async function checkPages(directory) {
  const root = resolve(directory);
  const html = await readFile(resolve(root, "index.html"), "utf8");
  assert(
    !html.includes("%BASE_URL%"),
    "The Pages base token was not compiled.",
  );
  assert(
    !html.includes("/src/main.tsx"),
    "Pages must serve the compiled build, not the source index.",
  );
  assert(
    html.includes('src="./assets/'),
    "Build Pages with a relative ./ base path.",
  );
  await stat(resolve(root, ".nojekyll"));
  const files = await readdir(resolve(root, "assets"));
  assert(
    files.some((f) => /^export\.worker-.*\.js$/.test(f)),
    "Mesh export worker is missing.",
  );
  let checked = 0;
  const checkReference = async (url, from) => {
    if (/^(?:data:|https?:|#)/.test(url)) return;
    assert(
      !url.startsWith("/"),
      `Root-absolute asset URL breaks project/nested Pages: ${url}`,
    );
    const file = resolve(
      dirname(from),
      decodeURIComponent(url.split(/[?#]/)[0]),
    );
    assert(
      !relative(root, file).split(sep).includes(".."),
      `Asset escapes the deployment: ${url}`,
    );
    assert((await stat(file)).isFile(), `Missing Pages asset: ${url}`);
    checked++;
  };
  for (const [, url] of html.matchAll(/(?:src|href)="([^"%]+)"/g))
    await checkReference(url, resolve(root, "index.html"));
  const editorPath = resolve(root, "material-editor.html");
  const editor = await readFile(editorPath, "utf8");
  assert(
    editor.includes('src="./assets/'),
    "Material editor must use compiled relative assets.",
  );
  assert(
    !editor.includes("/src/material-lab/main.tsx") &&
      !editor.includes("%BASE_URL%"),
    "Material editor HTML was not compiled.",
  );
  for (const [, url] of editor.matchAll(/(?:src|href)="([^"%]+)"/g))
    await checkReference(url, editorPath);
  for (const file of files.filter((f) => f.endsWith(".css"))) {
    const path = resolve(root, "assets", file);
    const css = await readFile(path, "utf8");
    for (const [, url] of css.matchAll(/url\(["']?([^"')]+)["']?\)/g))
      await checkReference(url, path);
  }
  for (const preset of ["canyon", "arches", "badlands"])
    await stat(resolve(root, "presets", `${preset}.webp`));
  for (const satmap of ["namib", "canyonlands", "iceland", "white-sands"])
    await stat(resolve(root, "satmaps", `${satmap}.webp`));
  await stat(resolve(root, "research", "satellite-texturing.md"));
  await stat(resolve(root, "research", "foam-implementation.md"));
  await stat(resolve(root, "research", "satmap-library.md"));
  await stat(resolve(root, "research", "sdf-stone-materials.md"));
  console.log(
    `Pages build verified: ${checked} local asset references, two HTML entries, worker, three terrains and four satellite sources.`,
  );
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
)
  await checkPages(process.argv[2] || "docs");
