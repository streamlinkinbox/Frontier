/** Encode owned RGBA bytes, never a WebGPU swapchain image after an await. */
export async function encodeFramePNG(
  pixels: Uint8Array,
  width: number,
  height: number,
  options: { bgra?: boolean; flipY?: boolean } = {},
): Promise<Blob> {
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    const sourceY = options.flipY ? height - 1 - y : y;
    const row = pixels.subarray(sourceY * width * 4, (sourceY + 1) * width * 4);
    rgba.set(row, y * width * 4);
  }
  if (options.bgra)
    for (let i = 0; i < rgba.length; i += 4)
      [rgba[i], rgba[i + 2]] = [rgba[i + 2], rgba[i]];
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  if (!context) throw new Error("Could not create the PNG encoder.");
  context.putImageData(new ImageData(rgba, width, height), 0, 0);
  return new Promise<Blob>((resolve, reject) =>
    canvas.toBlob(
      (blob) =>
        blob
          ? resolve(blob)
          : reject(new Error("Could not encode the viewport.")),
      "image/png",
    ),
  );
}
