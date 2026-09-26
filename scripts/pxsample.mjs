import fs from 'fs';
import { PNG } from 'pngjs';
const png = PNG.sync.read(fs.readFileSync(process.argv[2]));
const at = (x, y) => {
  const i = (y * png.width + x) * 4;
  return `rgb(${png.data[i]},${png.data[i+1]},${png.data[i+2]})`;
};
// carview: car occupies ~x 576-800, y 300-420
console.log('roof-ish  (680,330):', at(680, 330));
console.log('side      (640,385):', at(640, 385));
console.log('glass     (700,345):', at(700, 345));
console.log('wheel     (620,405):', at(620, 405));
console.log('sand ref  (200,500):', at(200, 500));
console.log('sky  ref  (640,150):', at(640, 150));
