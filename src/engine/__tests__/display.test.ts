import { afterEach, describe, expect, it, vi } from "vitest";
import { chooseGPUDisplay, unpackRGBA } from "../display";
import { nextVisibleFrame } from "../startup";

afterEach(() => vi.unstubAllGlobals());

describe("GPU display selection", () => {
  it("defaults Windows to safe bitmap display but leaves native explicitly selectable", () => {
    expect(chooseGPUDisplay("", "Chrome Windows NT 10.0 Win64")).toBe("safe");
    expect(chooseGPUDisplay("?display=native", "Windows NT 10.0")).toBe(
      "native",
    );
    expect(chooseGPUDisplay("?display=safe", "Linux")).toBe("safe");
    expect(chooseGPUDisplay("", "Linux")).toBe("native");
    expect(chooseGPUDisplay("?display=invalid", "Windows")).toBe("safe");
  });
});

describe("safe display readback", () => {
  it("unpacks padded GPU rows into an owned RGBA bitmap without touching source bytes", () => {
    const source = new Uint8Array(264);
    source.set([200, 120, 50, 255, 50, 100, 200, 255]);
    source.set([1, 2, 3, 255, 4, 5, 6, 255], 256);
    const original = source.slice();
    const dest = new Uint8ClampedArray(16);
    unpackRGBA(source, dest, 2, 2, 256);
    expect([...dest]).toEqual([
      200, 120, 50, 255, 50, 100, 200, 255, 1, 2, 3, 255, 4, 5, 6, 255,
    ]);
    expect(source).toEqual(original);
  });
  it("handles BGRA sources and rejects malformed pitches", () => {
    const dest = new Uint8ClampedArray(4);
    unpackRGBA(new Uint8Array([5, 10, 100, 255]), dest, 1, 1, 4, true);
    expect([...dest]).toEqual([100, 10, 5, 255]);
    expect(() => unpackRGBA(new Uint8Array(4), dest, 1, 1, 2)).toThrow(
      "Invalid",
    );
  });
});

describe("visible-frame startup gate", () => {
  function setup() {
    const doc = Object.assign(new EventTarget(), { hidden: true });
    const callbacks = new Map<number, FrameRequestCallback>();
    let index = 0;
    vi.stubGlobal("document", doc);
    vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
      callbacks.set(++index, callback);
      return index;
    });
    vi.stubGlobal("cancelAnimationFrame", (id: number) => callbacks.delete(id));
    const container = { clientWidth: 900, clientHeight: 600 } as HTMLElement;
    return { doc, callbacks, container };
  }
  it("does not render while hidden; resumes on a visible animation frame", async () => {
    const { doc, callbacks, container } = setup();
    const result = nextVisibleFrame(container, new AbortController().signal);
    expect(callbacks.size).toBe(0);
    doc.hidden = false;
    doc.dispatchEvent(new Event("visibilitychange"));
    expect(callbacks.size).toBe(1);
    callbacks.get(1)!(0);
    await expect(result).resolves.toBeUndefined();
  });
  it("cancels a hidden startup on dispose instead of later reviving an obsolete canvas", async () => {
    const { doc, callbacks, container } = setup();
    const controller = new AbortController();
    const result = nextVisibleFrame(container, controller.signal);
    controller.abort();
    await expect(result).rejects.toMatchObject({ name: "AbortError" });
    doc.hidden = false;
    doc.dispatchEvent(new Event("visibilitychange"));
    expect(callbacks.size).toBe(0);
  });
});
