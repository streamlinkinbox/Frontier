// Tiny PNG encoder (8-bit RGBA / 16-bit grayscale), zlib via CompressionStream('deflate').
const CRC = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC[(c ^ buf[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const out = new Uint8Array(12 + data.length);
  const dv = new DataView(out.buffer);
  dv.setUint32(0, data.length);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  dv.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
  return out;
}
async function zlib(raw) {
  const cs = new CompressionStream('deflate');
  const stream = new Blob([raw]).stream().pipeThrough(cs);
  return new Uint8Array(await new Response(stream).arrayBuffer());
}
// pixels: Uint8Array (RGBA8, w*h*4) or Uint16Array (gray16, w*h)
export async function encodePNG(w, h, pixels) {
  const g16 = pixels instanceof Uint16Array;
  const bpp = g16 ? 2 : 4;
  const raw = new Uint8Array((w * bpp + 1) * h);
  for (let y = 0; y < h; y++) {
    const o = y * (w * bpp + 1);
    raw[o] = 0;
    if (g16) {
      for (let x = 0; x < w; x++) { const v = pixels[y * w + x]; raw[o + 1 + x * 2] = v >> 8; raw[o + 2 + x * 2] = v & 255; }
    } else raw.set(pixels.subarray(y * w * 4, (y + 1) * w * 4), o + 1);
  }
  const ihdr = new Uint8Array(13);
  const dv = new DataView(ihdr.buffer);
  dv.setUint32(0, w); dv.setUint32(4, h);
  ihdr[8] = g16 ? 16 : 8; ihdr[9] = g16 ? 0 : 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  const parts = [new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', await zlib(raw)), chunk('IEND', new Uint8Array(0))];
  return new Blob(parts, { type: 'image/png' });
}
export function download(blob, name) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = name;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 4000);
}
