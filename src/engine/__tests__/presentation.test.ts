import { afterEach, describe, expect, it, vi } from "vitest";
import { encodeFramePNG } from "../presentation";

afterEach(() => vi.unstubAllGlobals());

function encoderMock(
  blob: Blob | null = new Blob(["png"], { type: "image/png" }),
) {
  const putImageData = vi.fn();
  vi.stubGlobal(
    "ImageData",
    class {
      constructor(
        public data: Uint8ClampedArray,
        public width: number,
        public height: number,
      ) {}
    },
  );
  vi.stubGlobal("document", {
    createElement: () => ({
      getContext: () => ({ putImageData }),
      toBlob: (callback: (value: Blob | null) => void) => callback(blob),
    }),
  });
  return putImageData;
}

describe("owned-frame PNG readback", () => {
  it("preserves RGBA channels without mutating the readback bytes", async () => {
    const put = encoderMock();
    const source = new Uint8Array([200, 120, 40, 255, 20, 60, 180, 255]);
    const original = source.slice();
    expect((await encodeFramePNG(source, 2, 1)).type).toBe("image/png");
    expect(put.mock.calls[0][0].data).toEqual(new Uint8ClampedArray(original));
    expect(source).toEqual(original);
  });
  it("swizzles a BGRA WebGPU target to RGBA", async () => {
    const put = encoderMock();
    await encodeFramePNG(new Uint8Array([40, 120, 200, 255]), 1, 1, {
      bgra: true,
    });
    expect([...put.mock.calls[0][0].data]).toEqual([200, 120, 40, 255]);
  });
  it("flips bottom-up WebGL readback without reversing the pixels in each row", async () => {
    const put = encoderMock();
    const bottom = [1, 2, 3, 255, 4, 5, 6, 255];
    const top = [7, 8, 9, 255, 10, 11, 12, 255];
    await encodeFramePNG(new Uint8Array([...bottom, ...top]), 2, 2, {
      flipY: true,
    });
    expect([...put.mock.calls[0][0].data]).toEqual([...top, ...bottom]);
  });
  it("reports an encoder failure rather than returning a broken download", async () => {
    encoderMock(null);
    await expect(encodeFramePNG(new Uint8Array(4), 1, 1)).rejects.toThrow(
      "Could not encode",
    );
  });
});
