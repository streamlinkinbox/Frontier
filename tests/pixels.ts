import { expect } from "@playwright/test";
import { PNG } from "pngjs";

/** Test the composed image, not just canvas existence, FPS, or a CSS backdrop. */
export function expectSandstone(buffer: Buffer) {
  const image = PNG.sync.read(buffer);
  let sandstone = 0;
  let opaque = 0;
  for (let i = 0; i < image.data.length; i += 4) {
    const [r, g, b, a] = image.data.subarray(i, i + 4);
    if (a > 240) opaque++;
    if (a > 240 && r > g * 1.18 && g > b * 1.1 && r > 55) sandstone++;
  }
  const count = image.width * image.height;
  expect(
    opaque / count,
    "The frame must be opaque, not a cleared bitmap",
  ).toBeGreaterThan(0.98);
  expect(
    sandstone / count,
    "The frame must contain sandstone, not just sky or a solid CSS background",
  ).toBeGreaterThan(0.08);
  return { width: image.width, height: image.height };
}
