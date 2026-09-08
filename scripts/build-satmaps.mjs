// Run with Node 22+: node --experimental-strip-types scripts/build-satmaps.mjs
// Downloads are cached outside Git. Re-running from the cached source images is
// deterministic. Only small image crops + extracted CLUT/detail data ship.
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import sharp from "sharp";
import { extractPalette, extractDetail } from "../src/engine/satmaps/pixels.ts";
const root = new URL("../", import.meta.url);
const sources = [
  {
    id: "namib",
    name: "Namib dunes",
    region: "Namibia",
    category: "Sandy",
    file: "namib.jpg",
    crop: [0.02, 0.02, 0.96, 0.96],
    source: "https://www.usgs.gov/media/images/namib-desert",
    image:
      "https://d9-wret.s3.us-west-2.amazonaws.com/assets/palladium/production/s3fs-public/thumbnails/image/namib_desert_0.jpg",
    credit: "USGS / NASA · Landsat 7 · Earth as Art 1",
    note: "Satellite composite; Earth as Art colors, not calibrated surface reflectance.",
  },
  {
    id: "canyonlands",
    name: "Canyonlands",
    region: "Utah, USA",
    category: "Rocky",
    file: "canyonlands.jpg",
    crop: [0.28, 0.52, 0.43, 0.43],
    source: "https://www.usgs.gov/media/images/canyonlands-national-park-4",
    image:
      "https://d9-wret.s3.us-west-2.amazonaws.com/assets/palladium/production/s3fs-public/thumbnails/image/5%20landsat.jpg",
    credit: "USGS / NASA · Landsat 8",
    note: "Enhanced-color satellite composite; cropped to the canyon terrain.",
  },
  {
    id: "iceland",
    name: "Volcanic coast",
    region: "Snæfellsnes, Iceland",
    category: "Green",
    file: "iceland.png",
    crop: [0.43, 0.39, 0.54, 0.6],
    source:
      "https://www.usgs.gov/media/images/landsat-9-image-snaefellsjokull-and-snaefellsnes-peninsula-west-iceland",
    image:
      "https://d9-wret.s3.us-west-2.amazonaws.com/assets/palladium/production/s3fs-public/media/images/LC09_L1TP_222014_20230809_20230809_02_T1_refl-crop1.png",
    credit: "USGS / NASA · Landsat 9 · August 9, 2023",
    note: "Bands 6/5/4 false-color composite, cropped to land. Green is enhanced vegetation.",
  },
  {
    id: "white-sands",
    name: "White Sands",
    region: "New Mexico, USA",
    category: "Mineral",
    file: "white-sands.png",
    crop: [0.24, 0.12, 0.56, 0.8],
    source:
      "https://www.usgs.gov/media/images/landsat-9-image-white-sands-national-park",
    image:
      "https://d9-wret.s3.us-west-2.amazonaws.com/assets/palladium/production/s3fs-public/media/images/LC08_L1TP_033037_20240503_20240513_02_T1_refl-crop.png",
    credit: "USGS / NASA · Landsat · White Sands",
    note: "Bands 6/5/4 false-color composite. USGS page and source filename disagree on satellite/date; no acquisition date is inferred here.",
  },
];
await mkdir(new URL(".cache/satmaps/", root), { recursive: true });
await mkdir(new URL("public/satmaps/", root), { recursive: true });
const library = [];
for (const item of sources) {
  const file = new URL(`.cache/satmaps/${item.file}`, root);
  let original;
  try {
    original = await readFile(file);
  } catch {
    const response = await fetch(item.image);
    if (!response.ok)
      throw new Error(`Download ${item.id}: ${response.status}`);
    original = Buffer.from(await response.arrayBuffer());
    await writeFile(file, original);
  }
  const metadata = await sharp(original).metadata();
  const [x, y, w, h] = item.crop;
  const image = sharp(original).extract({
    left: Math.floor(metadata.width * x),
    top: Math.floor(metadata.height * y),
    width: Math.floor(metadata.width * w),
    height: Math.floor(metadata.height * h),
  });
  // Store the crop losslessly so the runtime library can be reproduced exactly
  // without a network request: pixels and thumbnail share the same source.
  const crop = await image
    .resize(384, 256, { fit: "cover" })
    .webp({ lossless: true })
    .toBuffer();
  await writeFile(new URL(`public/satmaps/${item.id}.webp`, root), crop);
  const { data, info } = await sharp(crop)
    .resize(256, 256, { fit: "fill" })
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true });
  library.push({
    ...item,
    license: "Public domain (USGS)",
    sourceSHA256: createHash("sha256").update(original).digest("hex"),
    thumbnailSHA256: createHash("sha256").update(crop).digest("hex"),
    palette: extractPalette(data, info.width, info.height, "photo"),
    detail: extractDetail(data, info.width, info.height),
  });
}
await writeFile(
  new URL("src/engine/satmaps/library.json", root),
  JSON.stringify(library, null, 2) + "\n",
);
console.log(
  `Extracted ${library.length} satellite CLUTs: 256 RGB samples + 64² source-detail maps.`,
);
