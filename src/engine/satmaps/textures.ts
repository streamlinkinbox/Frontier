import type { Settings } from "../types";
import { getSatmap } from "./satmap";
import {
  DETAIL_SIZE,
  PALETTE_SAMPLES,
  detailMipmaps,
  paletteRGBA,
} from "./pixels";
import type { TerrainMaps } from "./terrainMaps";

/** Fixed-size resources: palette edits never allocate a new terrain volume. */
export class GLSatmapTextures {
  readonly palette: WebGLTexture;
  readonly detail: WebGLTexture;
  readonly terrain: WebGLTexture[];
  private paletteKey = "";
  private detailKey = "";
  constructor(
    private gl: WebGL2RenderingContext,
    width: number,
    height: number,
  ) {
    const texture = (w: number, h: number, srgb = false) => {
      const t = gl.createTexture()!;
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        srgb ? gl.SRGB8_ALPHA8 : gl.RGBA8,
        w,
        h,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        null,
      );
      return t;
    };
    this.palette = texture(PALETTE_SAMPLES, 1, true);
    this.detail = texture(DETAIL_SIZE, DETAIL_SIZE);
    this.terrain = [texture(width, height), texture(width, height)];
  }
  update(settings: Settings) {
    const gl = this.gl,
      asset = getSatmap(settings);
    if (asset.palette !== this.paletteKey) {
      gl.bindTexture(gl.TEXTURE_2D, this.palette);
      gl.texSubImage2D(
        gl.TEXTURE_2D,
        0,
        0,
        0,
        PALETTE_SAMPLES,
        1,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        paletteRGBA(asset.palette),
      );
      this.paletteKey = asset.palette;
    }
    if (asset.detail !== this.detailKey) {
      gl.bindTexture(gl.TEXTURE_2D, this.detail);
      for (const [level, mip] of detailMipmaps(asset.detail).entries())
        gl.texImage2D(
          gl.TEXTURE_2D,
          level,
          gl.RGBA8,
          mip.size,
          mip.size,
          0,
          gl.RGBA,
          gl.UNSIGNED_BYTE,
          mip.data,
        );
      gl.texParameteri(
        gl.TEXTURE_2D,
        gl.TEXTURE_MIN_FILTER,
        gl.LINEAR_MIPMAP_LINEAR,
      );
      this.detailKey = asset.detail;
    }
  }
  writeTerrain(maps: TerrainMaps, original = false) {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.terrain[original ? 1 : 0]);
    gl.texSubImage2D(
      gl.TEXTURE_2D,
      0,
      0,
      0,
      maps.width,
      maps.height,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      maps.pixels,
    );
  }
  bind(compare: boolean) {
    const gl = this.gl;
    for (const [i, texture] of [
      this.palette,
      this.detail,
      this.terrain[compare ? 1 : 0],
    ].entries()) {
      gl.activeTexture(gl.TEXTURE1 + i);
      gl.bindTexture(gl.TEXTURE_2D, texture);
    }
  }
  dispose() {
    for (const t of [this.palette, this.detail, ...this.terrain])
      this.gl.deleteTexture(t);
  }
}

export class GPUSatmapTextures {
  readonly palette: GPUTexture;
  readonly detail: GPUTexture;
  readonly terrain: GPUTexture[];
  private paletteKey = "";
  private detailKey = "";
  constructor(
    private device: GPUDevice,
    width: number,
    height: number,
  ) {
    const texture = (
      label: string,
      w: number,
      h: number,
      format: GPUTextureFormat,
      mipLevelCount = 1,
    ) =>
      device.createTexture({
        label,
        size: [w, h],
        format,
        mipLevelCount,
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
      });
    this.palette = texture(
      "Satellite CLUT (linear-filtered sRGB)",
      PALETTE_SAMPLES,
      1,
      "rgba8unorm-srgb",
    );
    this.detail = texture(
      "Satellite luminance detail",
      DETAIL_SIZE,
      DETAIL_SIZE,
      "rgba8unorm",
      7,
    );
    this.terrain = [
      texture("Live terrain catchment", width, height, "rgba8unorm"),
      texture("Original terrain catchment", width, height, "rgba8unorm"),
    ];
  }
  update(settings: Settings) {
    const asset = getSatmap(settings),
      queue = this.device.queue;
    if (asset.palette !== this.paletteKey) {
      queue.writeTexture(
        { texture: this.palette },
        paletteRGBA(asset.palette),
        { bytesPerRow: PALETTE_SAMPLES * 4 },
        [PALETTE_SAMPLES, 1],
      );
      this.paletteKey = asset.palette;
    }
    if (asset.detail !== this.detailKey) {
      for (const [mipLevel, mip] of detailMipmaps(asset.detail).entries())
        queue.writeTexture(
          { texture: this.detail, mipLevel },
          mip.data,
          { bytesPerRow: mip.size * 4 },
          [mip.size, mip.size],
        );
      this.detailKey = asset.detail;
    }
  }
  writeTerrain(maps: TerrainMaps, original = false) {
    this.device.queue.writeTexture(
      { texture: this.terrain[original ? 1 : 0] },
      maps.pixels,
      { bytesPerRow: maps.width * 4 },
      [maps.width, maps.height],
    );
  }
  dispose() {
    for (const t of [this.palette, this.detail, ...this.terrain]) t.destroy();
  }
}
