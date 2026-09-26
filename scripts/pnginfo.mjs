import fs from 'fs';
import { PNG } from 'pngjs';
const file = process.argv[2];
const png = PNG.sync.read(fs.readFileSync(file));
const W = png.width, H = png.height;
const cls = (r, g, b) => {
  if (r > 150 && g < 80 && b < 80) return 'R';   // watermark red
  if (b > 120 && b > r + 15 && g > 90) return 'W'; // water/sky blue
  if (r > 150 && g > 130 && b > 100 && r > b + 20) return 'S'; // sand
  if (r > 140 && g > 140 && b > 130 && Math.abs(r - b) < 30) return 'C'; // concrete/pale
  if (r < 90 && g < 90 && b < 90) return 'D';   // dark
  return '.';
};
const rows = 12, cols = 40;
let grid = [];
for (let ry = 0; ry < rows; ry++) {
  let line = '';
  for (let cx = 0; cx < cols; cx++) {
    let sr = 0, sg = 0, sb = 0, n = 0;
    for (let y = Math.floor(ry * H / rows); y < Math.floor((ry + 1) * H / rows); y += 8) {
      for (let x = Math.floor(cx * W / cols); x < Math.floor((cx + 1) * W / cols); x += 8) {
        const i = (y * W + x) * 4;
        sr += png.data[i]; sg += png.data[i + 1]; sb += png.data[i + 2]; n++;
      }
    }
    line += cls(sr / n, sg / n, sb / n);
  }
  grid.push(line);
}
console.log(file, `${W}x${H}`);
console.log(grid.join('\n'));
