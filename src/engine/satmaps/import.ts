import {
  extractPalette,
  extractDetail,
  type PaletteExtraction,
} from "./pixels";
import type { Settings } from "../types";

/** Local-only import. Store a compact color strip/detail signal in the project,
 * never a URL to somebody's file or an expiring browser object URL. */
export async function importSatmapFile(
  file: File,
  mode: PaletteExtraction,
): Promise<Partial<Settings>> {
  if (file.size > 12 * 1024 * 1024)
    throw new Error("Choose an image smaller than 12 MB.");
  if (!["image/png", "image/jpeg", "image/webp"].includes(file.type))
    throw new Error("Use a PNG, JPEG or WebP image.");
  let bitmap: ImageBitmap;
  try {
    bitmap = await createImageBitmap(file);
  } catch {
    throw new Error(
      "This image could not be decoded. Try a different PNG, JPEG or WebP.",
    );
  }
  try {
    if (
      bitmap.width * bitmap.height > 32 * 1024 * 1024 ||
      Math.max(bitmap.width, bitmap.height) > 16384
    )
      throw new Error(
        "This image is too large. Resize it to 32 megapixels or less.",
      );
    const scale = Math.min(1, 256 / Math.max(bitmap.width, bitmap.height));
    const canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.round(bitmap.width * scale));
    canvas.height = Math.max(1, Math.round(bitmap.height * scale));
    const ctx = canvas.getContext("2d", { willReadFrequently: true });
    if (!ctx)
      throw new Error("Image processing is unavailable in this browser.");
    ctx.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
    const pixels = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
    return {
      textureMode: "satmap",
      satmap: "custom",
      satmapName:
        file.name
          .replace(/\.[^.]+$/, "")
          .replace(/[\x00-\x1f]/g, "")
          .slice(0, 80) || "Imported palette",
      satmapPalette: extractPalette(pixels, canvas.width, canvas.height, mode),
      satmapDetailMap: extractDetail(pixels, canvas.width, canvas.height),
    };
  } finally {
    bitmap.close();
  }
}
