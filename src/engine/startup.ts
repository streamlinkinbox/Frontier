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

/** A hidden iframe may compile shaders, but must not acquire its first native
 * swapchain frame until the browser offers a visible rendering opportunity. */
export function nextVisibleFrame(
  container: HTMLElement,
  signal: AbortSignal,
): Promise<void> {
  return new Promise((resolve, reject) => {
    let raf = 0;
    const cleanup = () => {
      cancelAnimationFrame(raf);
      document.removeEventListener("visibilitychange", schedule);
      signal.removeEventListener("abort", abort);
    };
    const abort = () => {
      cleanup();
      reject(new DOMException("Viewport startup was cancelled", "AbortError"));
    };
    const schedule = () => {
      if (signal.aborted) {
        abort();
        return;
      }
      if (document.hidden || raf) return;
      raf = requestAnimationFrame(() => {
        raf = 0;
        if (
          !document.hidden &&
          container.clientWidth > 0 &&
          container.clientHeight > 0
        ) {
          cleanup();
          resolve();
        } else schedule();
      });
    };
    document.addEventListener("visibilitychange", schedule);
    signal.addEventListener("abort", abort, { once: true });
    schedule();
  });
}
