import { projectScreenOperation } from "./screenPaintProjection.js";
import { digitalBrushDabs, drawDigitalDabs } from "./digitalBrush.js";
import * as THREE from "three";
import {
  uvTopology,
  barycentricUV,
  uvBoundaryLoops,
} from "./textureUVGeometry.js";
import {
  maskRegionContains,
  validateMaskPaintGeometry,
  maskHasPaint,
} from "./textureMaskPaint.js";
import { compilePointGradient } from "./pointGradient.js";
import { colourMaskMatch } from "./textureMasks.js";

export const MASK_TILE_EDGE = 256;
export const MASK_ATLAS_WIDTH = MASK_TILE_EDGE * 4;
const caches = new WeakMap();
const makeCanvas = () => {
  const c = document.createElement("canvas");
  c.width = MASK_ATLAS_WIDTH;
  c.height = MASK_TILE_EDGE;
  return c;
};
export function maskGeometryCache(geometry) {
  if (caches.has(geometry)) return caches.get(geometry);
  const topology = uvTopology(geometry),
    paths = new Map(),
    loops = uvBoundaryLoops(geometry);
  function path(region) {
    const key = JSON.stringify(region);
    if (!paths.has(key)) {
      const p = new Path2D();
      if (region.kind === "face") {
        const t = topology.triangles[region.id];
        if (maskRegionContains(region, t)) {
          p.moveTo(...t.points[0]);
          p.lineTo(...t.points[1]);
          p.lineTo(...t.points[2]);
          p.closePath();
        }
      } else {
        // Exact triangle-union boundary contours, including holes. Clipping
        // thousands of overlapping triangle subpaths for every new brush
        // canvas was needlessly expensive (especially on software GPUs).
        for (const loop of loops)
          if (
            (region.kind === "surface" || loop.part?.id === region.partId) &&
            (region.kind !== "island" || loop.chartId === region.id)
          ) {
            p.moveTo(...loop.points[0]);
            for (const point of loop.points.slice(1)) p.lineTo(...point);
            p.closePath();
          }
      }
      paths.set(key, p);
    }
    return paths.get(key);
  }
  // Barycentric object coordinates for actual atlas texels, never a synthetic
  // plane or slice substituted for the teapot's gradient field.
  let lookup = null;
  function positions() {
    if (lookup) return lookup;
    geometry.computeBoundingSphere();
    const sphere = geometry.boundingSphere,
      p = geometry.attributes.position,
      xyz = new Float32Array(MASK_ATLAS_WIDTH * MASK_TILE_EDGE * 3),
      occupied = new Uint8Array(MASK_ATLAS_WIDTH * MASK_TILE_EDGE);
    for (const t of topology.triangles) {
      const loX = Math.max(0, Math.floor(t.min[0] * MASK_TILE_EDGE)),
        hiX = Math.min(
          MASK_ATLAS_WIDTH - 1,
          Math.ceil(t.max[0] * MASK_TILE_EDGE),
        ),
        loY = Math.max(0, Math.floor((1 - t.max[1]) * MASK_TILE_EDGE)),
        hiY = Math.min(
          MASK_TILE_EDGE - 1,
          Math.ceil((1 - t.min[1]) * MASK_TILE_EDGE),
        );
      for (let y = loY; y <= hiY; y++)
        for (let x = loX; x <= hiX; x++) {
          const w = barycentricUV(
            [(x + 0.5) / MASK_TILE_EDGE, 1 - (y + 0.5) / MASK_TILE_EDGE],
            t,
          );
          if (!w) continue;
          const k = y * MASK_ATLAS_WIDTH + x;
          occupied[k] = 1;
          for (let c = 0; c < 3; c++)
            xyz[k * 3 + c] =
              (t.ids.reduce(
                (sum, id, j) => sum + p.array[id * 3 + c] * w[j],
                0,
              ) -
                sphere.center.getComponent(c)) /
              sphere.radius;
        }
    }
    return (lookup = { xyz, occupied });
  }
  const cache = { path, positions };
  caches.set(geometry, cache);
  return cache;
}
export function rasterizeMaskPaint(mask, geometry) {
  validateMaskPaintGeometry(mask?.paint, geometry);
  const canvas = makeCanvas(),
    ctx = canvas.getContext("2d"),
    cache = maskGeometryCache(geometry);
  ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
  for (const op of mask?.paint?.operations || []) {
    ctx.save();
    // Projected strokes already enforce their real triangle/selection scope.
    // Keep their empty-gutter padding for seamless filtered mask sampling.
    if (!op.digital?.projection) ctx.clip(cache.path(op.region));
    const grey = Math.round(op.value * 255);
    if (op.kind === "region") {
      ctx.fillStyle = `rgba(${grey},${grey},${grey},${op.opacity})`;
      ctx.fill(cache.path(op.region));
    } else if (op.digital?.projection) {
      const grey = Math.round(op.value * 255),
        color = `#${grey.toString(16).padStart(2, "0").repeat(3)}`;
      const mark = projectScreenOperation(
        {
          region: op.region,
          projection: op.digital.projection,
          points: op.points.map((p, i) => [...p, op.digital.pressure[i]]),
          brush: op.digital.brush,
          seed: op.digital.seed,
          kind: op.digital.operation,
        },
        geometry,
        color,
        false,
      );
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalAlpha = op.opacity;
      ctx.globalCompositeOperation =
        op.digital.operation === "erase" ? "destination-out" : "source-over";
      ctx.drawImage(mark, 0, 0);
    } else if (op.digital) {
      // Shared digital tip/pressure/grain renderer. Erasing removes the painted
      // overlay, restoring the underlying fill/gradient/source, not black ink.
      const mark = makeCanvas(),
        ink = mark.getContext("2d");
      ink.setTransform(1, 0, 0, -1, 0, MASK_TILE_EDGE);
      const color = `#${grey.toString(16).padStart(2, "0").repeat(3)}`;
      const points = op.points.map((p, i) => [...p, op.digital.pressure[i]]);
      drawDigitalDabs(
        ink,
        digitalBrushDabs(
          points,
          op.digital.brush,
          op.digital.seed,
          MASK_TILE_EDGE,
          color,
          false,
        ),
        op.digital.brush,
      );
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalAlpha = op.opacity;
      ctx.globalCompositeOperation =
        op.digital.operation === "erase" ? "destination-out" : "source-over";
      ctx.drawImage(mark, 0, 0);
    } else {
      // Spaced radial dabs provide a genuine soft/hard UV brush. Each stroke
      // is clipped to its semantic region, so a lid stroke cannot touch Body.
      const dab = (x, y) => {
        const g = ctx.createRadialGradient(x, y, 0, x, y, op.radius),
          ink = `rgba(${grey},${grey},${grey},${op.opacity})`;
        g.addColorStop(0, ink);
        g.addColorStop(Math.min(0.999, op.hardness), ink);
        g.addColorStop(1, `rgba(${grey},${grey},${grey},0)`);
        ctx.fillStyle = g;
        ctx.beginPath();
        ctx.arc(x, y, op.radius, 0, Math.PI * 2);
        ctx.fill();
      };
      dab(...op.points[0]);
      for (let i = 1; i < op.points.length; i++) {
        const a = op.points[i - 1],
          b = op.points[i],
          n = Math.ceil(
            Math.hypot(b[0] - a[0], b[1] - a[1]) / (op.radius * 0.35),
          );
        for (let j = 1; j <= n; j++)
          dab(a[0] + ((b[0] - a[0]) * j) / n, a[1] + ((b[1] - a[1]) * j) / n);
      }
    }
    ctx.restore();
  }
  return canvas;
}
export function maskColourSource(layer, doc) {
  const sourceLayer =
    layer?.kind === "folder"
      ? doc.layers.find((l) => {
          let node = l;
          while (node?.parentId) {
            if (node.parentId === layer.id)
              return l.channels.some(
                (id) =>
                  l.channelSettings[id]?.texture ||
                  l.channelSettings[id]?.generator?.result,
              );
            node = doc.layers.find((v) => v.id === node.parentId);
          }
          return false;
        })
      : layer;
  const settings = sourceLayer?.channels.map(
    (id) => sourceLayer.channelSettings[id],
  );
  const setting = settings?.find((s) => s.texture || s.generator?.result);
  return setting?.texture || setting?.generator?.result || null;
}
export async function readMaskSource(source) {
  if (!source) return null;
  const image = new Image();
  image.src = source.dataUrl;
  await image.decode();
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = MASK_TILE_EDGE;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  ctx.drawImage(image, 0, 0, MASK_TILE_EDGE, MASK_TILE_EDGE);
  return ctx.getImageData(0, 0, MASK_TILE_EDGE, MASK_TILE_EDGE).data;
}
export function rasterizeMaskField(mask, geometry, source = null) {
  const strokes = rasterizeMaskPaint(mask, geometry)
      .getContext("2d")
      .getImageData(0, 0, MASK_ATLAS_WIDTH, MASK_TILE_EDGE).data,
    output = makeCanvas(),
    ctx = output.getContext("2d"),
    image = ctx.createImageData(MASK_ATLAS_WIDTH, MASK_TILE_EDGE),
    sample =
      mask.kind === "gradient" ? compilePointGradient(mask.gradient) : null,
    lookup = sample ? maskGeometryCache(geometry).positions() : null;
  for (let k = 0; k < MASK_ATLAS_WIDTH * MASK_TILE_EDGE; k++) {
    let raw = mask.fill;
    if (sample) {
      const c = sample(Array.from(lookup.xyz.subarray(k * 3, k * 3 + 3)));
      raw = c[0] * 0.2126 + c[1] * 0.7152 + c[2] * 0.0722;
    }
    if (mask.kind === "color") {
      const n =
        (Math.floor(k / MASK_ATLAS_WIDTH) * MASK_TILE_EDGE +
          (k % MASK_TILE_EDGE)) *
        4;
      raw = source
        ? (colourMaskMatch(
            [source[n] / 255, source[n + 1] / 255, source[n + 2] / 255],
            { ...mask, inverted: false, paint: null },
          ) *
            source[n + 3]) /
          255
        : 0;
    }
    const a = strokes[k * 4 + 3] / 255;
    raw = raw * (1 - a) + (strokes[k * 4] / 255) * a;
    if (mask.inverted) raw = 1 - raw;
    const grey = Math.round(
      255 * (mask.enabled ? 1 + ((raw - 1) * mask.strength) / 100 : 1),
    );
    image.data[k * 4] = image.data[k * 4 + 1] = image.data[k * 4 + 2] = grey;
    image.data[k * 4 + 3] = 255;
  }
  ctx.putImageData(image, 0, 0);
  return output;
}
export function createMaskCoverageTexture(geometry) {
  const canvas = makeCanvas(),
    texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.NoColorSpace;
  texture.generateMipmaps = false;
  texture.minFilter = texture.magFilter = THREE.LinearFilter;
  const uniforms = {
    uPaintedMaskAtlas: { value: texture },
    uPaintedMaskEnabled: { value: 0 },
  };
  let ticket = 0,
    disposed = false;
  return {
    uniforms,
    async update(owners, doc) {
      const job = ++ticket,
        masks = owners.filter((l) => maskHasPaint(l.mask));
      if (!masks.length) {
        uniforms.uPaintedMaskEnabled.value = 0;
        return;
      }
      const fields = await Promise.all(
        masks.map(async (l) =>
          rasterizeMaskField(
            l.mask,
            geometry,
            l.mask.kind === "color"
              ? await readMaskSource(maskColourSource(l, doc))
              : null,
          ),
        ),
      );
      if (disposed || job !== ticket) return;
      const ctx = canvas.getContext("2d");
      ctx.globalCompositeOperation = "source-over";
      ctx.fillStyle = "white";
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.globalCompositeOperation = "multiply";
      for (const f of fields) ctx.drawImage(f, 0, 0);
      ctx.globalCompositeOperation = "source-over";
      uniforms.uPaintedMaskEnabled.value = 1;
      texture.needsUpdate = true;
    },
    dispose() {
      disposed = true;
      ticket++;
      texture.dispose();
    },
  };
}
export function installMaskPaintCoverage(material, uniforms, mode = "alpha") {
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms);
    shader.fragmentShader =
      "uniform sampler2D uPaintedMaskAtlas; uniform int uPaintedMaskEnabled;\n" +
      shader.fragmentShader;
    const value =
      "(uPaintedMaskEnabled==1?texture2D(uPaintedMaskAtlas,vec2(vAuthoringUv.x/4.,vAuthoringUv.y)).r:1.)";
    shader.fragmentShader =
      mode === "gradient"
        ? shader.fragmentShader.replace(
            "float coverage=uPointGradientOpacity",
            `float coverage=${value}*uPointGradientOpacity`,
          )
        : shader.fragmentShader.replace(
            "#include <alphatest_fragment>",
            `diffuseColor.a*=${value};\n#include <alphatest_fragment>`,
          );
  };
  material.customProgramCacheKey = () =>
    cache() + `|painted-uv-mask-v1-${mode}`;
}
