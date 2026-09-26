import fs from 'fs';
import { PNG } from 'pngjs';
const png = PNG.sync.read(fs.readFileSync(process.argv[2]));
const cls = (r, g, b) => {
  if (b > r + 25 && b > 60 && g > r) return 'B';       // blue paint
  if (b > r + 15 && r < 70 && g < 70) return 'b';      // dark blue/glass
  if (r < 75 && g < 75 && b < 75) return 'D';          // black (tire/dark)
  if (r > 140 && g > 120 && b > 85 && r > b + 15) return 'S'; // sand
  if (r > 140 && g > 140 && b > 135) return 'C';       // pale/concrete
  if (b > r + 10 && g > 100) return 'W';               // sky/water
  return '.';
};
const rows = 18, cols = 64, W = png.width, H = png.height;
for (let ry = 0; ry < rows; ry++) {
  let line = '';
  for (let cx = 0; cx < cols; cx++) {
    let sr = 0, sg = 0, sb = 0, n = 0;
    for (let y = Math.floor(ry * H / rows); y < Math.floor((ry + 1) * H / rows); y += 4) {
      for (let x = Math.floor(cx * W / cols); x < Math.floor((cx + 1) * W / cols); x += 4) {
        const i = (y * W + x) * 4;
        sr += png.data[i]; sg += png.data[i + 1]; sb += png.data[i + 2]; n++;
      }
    }
    line += cls(sr / n, sg / n, sb / n);
  }
  console.log(line);
}
