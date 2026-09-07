/** A completed command submission is not proof that a canvas contains an image. */
export function hasVisibleFrame(pixels: ArrayLike<number>): boolean {
  if (pixels.length < 4) return false;
  const min = [255, 255, 255];
  const max = [0, 0, 0];
  let opaque = 0;
  for (let i = 0; i + 3 < pixels.length; i += 4) {
    if (pixels[i + 3] < 128) continue;
    opaque++;
    for (let c = 0; c < 3; c++) {
      min[c] = Math.min(min[c], pixels[i + c]);
      max[c] = Math.max(max[c], pixels[i + c]);
    }
  }
  return opaque >= pixels.length / 8 && max.some((v, c) => v - min[c] > 8);
}

export async function withTimeout<T>(
  task: Promise<T>,
  ms: number,
  message: string,
): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    return await Promise.race([
      task,
      new Promise<never>((_, reject) => {
        timer = setTimeout(() => reject(new Error(message)), ms);
      }),
    ]);
  } finally {
    if (timer !== undefined) clearTimeout(timer);
  }
}
