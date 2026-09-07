export type GPUDisplayMode = "native" | "safe";

/** Windows workaround: retain GPU compute/raymarching, bypass its swapchain.
 * The fast native path remains explicitly selectable on any platform. */
export function chooseGPUDisplay(
  search: string,
  userAgent: string,
): GPUDisplayMode {
  const requested = new URLSearchParams(search).get("display");
  if (requested === "safe" || requested === "native") return requested;
  return /Windows/i.test(userAgent) ? "safe" : "native";
}

/** Copy only owned, mapped GPU bytes; never drawImage() a GPU canvas. */
export function unpackRGBA(
  source: Uint8Array,
  destination: Uint8ClampedArray,
  width: number,
  height: number,
  rowBytes: number,
  bgra = false,
) {
  if (
    width < 1 ||
    height < 1 ||
    rowBytes < width * 4 ||
    source.length < (height - 1) * rowBytes + width * 4 ||
    destination.length !== width * height * 4
  )
    throw new Error("Invalid frame readback dimensions.");
  for (let y = 0; y < height; y++)
    destination.set(
      source.subarray(y * rowBytes, y * rowBytes + width * 4),
      y * width * 4,
    );
  if (bgra)
    for (let i = 0; i < destination.length; i += 4)
      [destination[i], destination[i + 2]] = [
        destination[i + 2],
        destination[i],
      ];
}

/** Software-backed DOM bitmap presentation. GPU still produces EVERY frame.
 * Both canvases are ordinary CPU 2D canvases, not native GPU canvas images. */
export class BitmapPresenter {
  private context: CanvasRenderingContext2D;
  private staging = document.createElement("canvas");
  private stagingContext: CanvasRenderingContext2D;
  private image?: ImageData;
  frames = 0;
  constructor(private canvas: HTMLCanvasElement) {
    const options: CanvasRenderingContext2DSettings = {
      alpha: false,
      willReadFrequently: true,
    };
    const context = canvas.getContext("2d", options);
    const staging = this.staging.getContext("2d", options);
    if (!context || !staging)
      throw new Error("Could not create the safe display bitmap.");
    this.context = context;
    this.stagingContext = staging;
  }
  present(bytes: Uint8Array, width: number, height: number, rowBytes: number) {
    if (this.image?.width !== width || this.image.height !== height) {
      this.image = new ImageData(width, height);
      this.staging.width = width;
      this.staging.height = height;
    }
    unpackRGBA(bytes, this.image.data, width, height, rowBytes);
    if (this.canvas.width === width && this.canvas.height === height) {
      this.context.putImageData(this.image, 0, 0);
    } else {
      this.stagingContext.putImageData(this.image, 0, 0);
      this.context.imageSmoothingEnabled = true;
      this.context.globalCompositeOperation = "copy";
      this.context.drawImage(
        this.staging,
        0,
        0,
        this.canvas.width,
        this.canvas.height,
      );
    }
    this.frames++;
  }
  sample(): Uint8ClampedArray {
    // This is a software-backed bitmap, NOT a WebGPU swapchain readback.
    const w = this.canvas.width,
      h = this.canvas.height;
    const data = this.context.getImageData(0, 0, w, h).data;
    const pixels = new Uint8ClampedArray(32 * 32 * 4);
    for (let y = 0; y < 32; y++)
      for (let x = 0; x < 32; x++) {
        const sx = Math.min(w - 1, Math.floor(((x + 0.5) * w) / 32));
        const sy = Math.min(h - 1, Math.floor(((y + 0.5) * h) / 32));
        const offset = (sy * w + sx) * 4;
        pixels.set(data.subarray(offset, offset + 4), (y * 32 + x) * 4);
      }
    return pixels;
  }
}
