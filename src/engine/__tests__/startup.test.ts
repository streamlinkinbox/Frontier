import { afterEach, describe, expect, it, vi } from "vitest";
import { hasVisibleFrame, withTimeout } from "../startup";

afterEach(() => vi.useRealTimers());

describe("first-frame verification", () => {
  it("rejects transparent canvases and a uniform clear color", () => {
    expect(hasVisibleFrame(new Uint8Array(32 * 32 * 4))).toBe(false);
    const clear = new Uint8Array(32 * 32 * 4);
    for (let i = 0; i < clear.length; i += 4) clear.set([56, 66, 72, 255], i);
    expect(hasVisibleFrame(clear)).toBe(false);
  });
  it("requires actual opaque pixels with spatial color variation", () => {
    const pixels = new Uint8Array(32 * 32 * 4);
    for (let i = 0; i < pixels.length; i += 4)
      pixels.set([90 + ((i / 4) % 120), 110, 125, 255], i);
    expect(hasVisibleFrame(pixels)).toBe(true);
    for (let i = 3; i < pixels.length; i += 4) pixels[i] = 0;
    expect(hasVisibleFrame(pixels)).toBe(false);
  });
});

describe("bounded renderer startup", () => {
  it("cancels the deadline after startup completes", async () => {
    vi.useFakeTimers();
    expect(await withTimeout(Promise.resolve("ready"), 1000, "timeout")).toBe(
      "ready",
    );
    expect(vi.getTimerCount()).toBe(0);
  });
  it("rejects stalled startup instead of leaving an endless blank viewport", async () => {
    vi.useFakeTimers();
    const task = withTimeout(
      new Promise<never>(() => {}),
      1000,
      "Renderer timed out",
    );
    const rejection = expect(task).rejects.toThrow("Renderer timed out");
    await vi.advanceTimersByTimeAsync(1000);
    await rejection;
    expect(vi.getTimerCount()).toBe(0);
  });
});
