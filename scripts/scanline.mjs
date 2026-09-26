import fs from 'fs';
import { PNG } from 'pngjs';
const png = PNG.sync.read(fs.readFileSync(process.argv[2]));
const at = (x, y) => {
  const i = (y * png.width + x) * 4;
  return `(${String(png.data[i]).padStart(3)},${String(png.data[i+1]).padStart(3)},${String(png.data[i+2]).padStart(3)})`;
};
for (const y of [380, 410, 440, 470, 500]) {
  let line = `y=${y}: `;
  for (const x of [360, 420, 480, 540, 600, 660, 720, 780, 840, 900]) line += `${x}:${at(x, y)}  `;
  console.log(line);
}
