/** Browser/worker-independent satellite color extraction. No authored color stops. */
export const PALETTE_SAMPLES = 256;
export const DETAIL_SIZE = 64;
export type PaletteExtraction = "photo" | "strip";

export function bytesToHex(bytes: ArrayLike<number>): string {
  return Array.from(bytes, (v) => v.toString(16).padStart(2, "0")).join("");
}
export function hexToBytes(hex: string): Uint8Array {
  const result = new Uint8Array(hex.length / 2);
  for (let i = 0; i < result.length; i++)
    result[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  return result;
}
const luminance = (r: number, g: number, b: number) =>
  r * 0.2126 + g * 0.7152 + b * 0.0722;

/** Photo: trimmed luminance-quantile CLUT; strip: preserve the artist's order.
 * Nearby samples are averaged to suppress single-pixel noise, not replaced by
 * invented gradients. Fully transparent pixels never enter the palette. */
export function extractPalette(
  rgba: ArrayLike<number>,
  width: number,
  height: number,
  mode: PaletteExtraction,
): string {
  if (rgba.length !== width * height * 4 || width < 1 || height < 1)
    throw new Error("Invalid image pixels.");
  const colors: number[][] = [];
  if (mode === "strip") {
    const horizontal = width >= height;
    for (let i = 0; i < (horizontal ? width : height); i++) {
      const sum = [0, 0, 0];
      let count = 0;
      for (let j = 0; j < (horizontal ? height : width); j++) {
        const p = (horizontal ? j * width + i : i * width + j) * 4;
        if (rgba[p + 3] < 128) continue;
        for (let c = 0; c < 3; c++) sum[c] += rgba[p + c];
        count++;
      }
      if (count) colors.push(sum.map((v) => v / count));
    }
  } else {
    for (let p = 0; p < rgba.length; p += 4)
      if (rgba[p + 3] >= 128) colors.push([rgba[p], rgba[p + 1], rgba[p + 2]]);
    colors.sort(
      (a, b) => luminance(a[0], a[1], a[2]) - luminance(b[0], b[1], b[2]),
    );
  }
  if (!colors.length)
    throw new Error("This image is fully transparent. Choose an opaque image.");
  const output = new Uint8Array(PALETTE_SAMPLES * 3);
  const trim = mode === "photo" ? 0.02 : 0;
  const radius =
    mode === "photo" ? Math.max(1, Math.floor(colors.length / 512)) : 0;
  for (let i = 0; i < PALETTE_SAMPLES; i++) {
    const pos =
      (trim + ((1 - 2 * trim) * i) / (PALETTE_SAMPLES - 1)) *
      (colors.length - 1);
    const sum = [0, 0, 0];
    let count = 0;
    for (let offset = -radius; offset <= radius; offset++) {
      const p = Math.max(0, Math.min(colors.length - 1, pos + offset));
      const lo = Math.floor(p),
        hi = Math.min(colors.length - 1, lo + 1),
        t = p - lo;
      for (let c = 0; c < 3; c++)
        sum[c] += colors[lo][c] * (1 - t) + colors[hi][c] * t;
      count++;
    }
    for (let c = 0; c < 3; c++) output[i * 3 + c] = Math.round(sum[c] / count);
  }
  return bytesToHex(output);
}

/** Area-average source luminance into a small, portable detail map. This is
 * artistic image detail, NOT a DEM or a measured rock normal map. */
export function extractDetail(
  rgba: ArrayLike<number>,
  width: number,
  height: number,
): string {
  const result = new Uint8Array(DETAIL_SIZE ** 2);
  const values: number[] = [];
  for (let y = 0; y < DETAIL_SIZE; y++)
    for (let x = 0; x < DETAIL_SIZE; x++) {
      let total = 0,
        count = 0;
      for (
        let sy = Math.floor((y * height) / DETAIL_SIZE);
        sy <
        Math.max(
          Math.floor((y * height) / DETAIL_SIZE) + 1,
          Math.floor(((y + 1) * height) / DETAIL_SIZE),
        );
        sy++
      )
        for (
          let sx = Math.floor((x * width) / DETAIL_SIZE);
          sx <
          Math.max(
            Math.floor((x * width) / DETAIL_SIZE) + 1,
            Math.floor(((x + 1) * width) / DETAIL_SIZE),
          );
          sx++
        ) {
          const p =
            (Math.min(sy, height - 1) * width + Math.min(sx, width - 1)) * 4;
          if (rgba[p + 3] < 128) continue;
          total += luminance(rgba[p], rgba[p + 1], rgba[p + 2]);
          count++;
        }
      values.push(count ? total / count : 127.5);
    }
  const sorted = [...values].sort((a, b) => a - b);
  const lo = sorted[Math.floor(sorted.length * 0.02)],
    hi = sorted[Math.floor(sorted.length * 0.98)];
  for (let i = 0; i < values.length; i++)
    result[i] =
      hi - lo < 1
        ? 128
        : Math.round(
            Math.max(0, Math.min(1, (values[i] - lo) / (hi - lo))) * 255,
          );
  return bytesToHex(result);
}

export function paletteRGBA(hex: string): Uint8Array {
  const rgb = hexToBytes(hex),
    rgba = new Uint8Array(PALETTE_SAMPLES * 4);
  for (let i = 0; i < PALETTE_SAMPLES; i++) {
    rgba.set(rgb.subarray(i * 3, i * 3 + 3), i * 4);
    rgba[i * 4 + 3] = 255;
  }
  return rgba;
}
export function detailMipmaps(
  hex: string,
): { size: number; data: Uint8Array }[] {
  let gray = hexToBytes(hex),
    size = DETAIL_SIZE;
  const levels: { size: number; data: Uint8Array }[] = [];
  while (true) {
    const data = new Uint8Array(size * size * 4);
    for (let i = 0; i < gray.length; i++)
      data.set([gray[i], gray[i], gray[i], 255], i * 4);
    levels.push({ size, data });
    if (size === 1) return levels;
    const next = new Uint8Array((size / 2) ** 2);
    for (let y = 0; y < size / 2; y++)
      for (let x = 0; x < size / 2; x++) {
        const i = y * 2 * size + x * 2;
        next[(y * size) / 2 + x] = Math.round(
          (gray[i] + gray[i + 1] + gray[i + size] + gray[i + size + 1]) / 4,
        );
      }
    size /= 2;
    gray = next;
  }
}
