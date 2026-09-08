import { test, expect, type Page } from "@playwright/test";
import { PNG } from "pngjs";
import { readFile } from "node:fs/promises";

async function ready(page: Page, renderer: string) {
  await page.goto(`/?renderer=${renderer}&display=safe&panel=foam`);
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  await expect(page.locator(".has-error")).toHaveCount(0);
  expect(await page.evaluate(() => window.__frontier!.backend.name)).toBe(
    renderer === "webgl" ? "WebGL2" : "WebGPU",
  );
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.pause();
    e.busy = true;
    await e.backend.sync();
  });
}
function delta(a: Buffer, b: Buffer) {
  const x = PNG.sync.read(a).data,
    y = PNG.sync.read(b).data;
  let sum = 0;
  for (let i = 0; i < x.length; i++)
    if (i % 4 !== 3) sum += Math.abs(x[i] - y[i]);
  return sum / ((x.length / 4) * 3);
}

for (const renderer of ["webgl", "webgpu"]) {
  test(`${renderer}: persistent foam advects, decays, respects banks/roofs, and never edits the SDF`, async ({
    page,
  }) => {
    const errors: string[] = [];
    page.on("pageerror", (e) => errors.push(e.message));
    page.on("console", (m) => {
      if (m.type() === "error") errors.push(m.text());
    });
    await ready(page, renderer);
    const result = await page.evaluate(async () => {
      const e = window.__frontier!,
        b: any = e.backend;
      const { packUniforms } = await import("/src/engine/uniforms.ts");
      const n = b.size;
      const data = new Float32Array(n.x * n.y * n.z * 4);
      const box = (x: number, y: number, z: number, h: number[]) => {
        const q = [Math.abs(x) - h[0], Math.abs(y) - h[1], Math.abs(z) - h[2]];
        return (
          Math.hypot(...q.map((v) => Math.max(v, 0))) +
          Math.min(Math.max(...q), 0)
        );
      };
      for (let z = 0; z < n.z; z++)
        for (let y = 0; y < n.y; y++)
          for (let x = 0; x < n.x; x++) {
            const p = [
              -48 + ((x + 0.5) * 96) / n.x,
              -10 + ((y + 0.5) * 48) / n.y,
              -48 + ((z + 0.5) * 96) / n.z,
            ];
            data[((z * n.y + y) * n.x + x) * 4] = Math.min(
              p[1] + 2,
              box(p[0], p[1] - 5, p[2], [4, 0.8, 4]),
              box(p[0] - 9, p[1] - 1, p[2], [1, 5, 8]),
            );
          }
      b.writeVolume(data);
      const original = await b.readVolume();
      const settings = {
        ...e.settings,
        foamQuality: "standard",
        foamBudget: "compact",
        foamView: "surface",
        water: true,
        waterFoam: 0,
        waterCurrent: 1,
        waterFlowMode: "directional",
        waterDirection: 0,
        waterReverse: false,
        wind: 0,
        foamLifetime: 6,
      };
      const frame: any = {
        ...(e as any).makeFrame(),
        settings,
        time: 0,
        width: 160,
        height: 120,
        brush: null,
      };
      b.resetFoam();
      b.render(frame);
      await b.sync();
      const f = b.foam,
        field = b.name === "WebGPU" ? b.fields[b.current] : b.texture;
      const setFrame = () => {
        const values = packUniforms(frame, n);
        if (b.name === "WebGPU")
          b.device.queue.writeBuffer(b.uniform, 0, values);
        else {
          b.gl.bindBuffer(b.gl.UNIFORM_BUFFER, b.uniform);
          b.gl.bufferSubData(b.gl.UNIFORM_BUFFER, 0, values);
        }
      };
      const initial = await f.inspect();
      const gN = f.profile.flow;
      const at = (x: number, z: number) =>
        (Math.floor(((z + 48) / 96) * gN) * gN +
          Math.floor(((x + 48) / 96) * gN)) *
        4;
      const belowRoof = initial.geometry[at(0, 0) + 1],
        bankDepth = initial.geometry[at(9, 0) + 1];
      const size = f.density.width,
        dye = new Float32Array(size * size * 4);
      for (let z = 0; z < size; z++)
        for (let x = 0; x < size; x++) {
          const px = -48 + ((x + 0.5) * 96) / size,
            pz = -48 + ((z + 0.5) * 96) / size;
          dye[(z * size + x) * 4] =
            Math.exp(-((px + 16) ** 2 + pz * pz) / (2 * 1.5 ** 2)) * 0.6;
        }
      if (b.name === "WebGPU") {
        const { floatToHalf } = await import("/src/engine/math.ts");
        b.device.queue.writeTexture(
          { texture: f.density.handle },
          Uint16Array.from(dye, floatToHalf),
          { bytesPerRow: size * 8 },
          [size, size],
        );
      } else {
        const gl = b.gl;
        gl.bindTexture(gl.TEXTURE_2D, f.density.handle);
        gl.texSubImage2D(
          gl.TEXTURE_2D,
          0,
          0,
          0,
          size,
          size,
          gl.RGBA,
          gl.FLOAT,
          dye,
        );
      }
      const summarize = (a: Float32Array) => {
        let q = 0,
          x = 0;
        for (let z = 0; z < size; z++)
          for (let i = 0; i < size; i++) {
            const v = a[(z * size + i) * 4];
            q += v;
            x += v * (-48 + ((i + 0.5) * 96) / size);
          }
        return { q, x: x / q };
      };
      const start = summarize((await f.inspect()).density);
      for (let i = 1; i <= 30; i++) {
        frame.time = i / 30;
        setFrame();
        f.update(frame, field, true);
        if (i % 10 === 0) await b.sync();
      }
      await b.sync();
      const moving = await f.inspect(),
        end = summarize(moving.density);
      const beforePause = moving.density.slice(),
        ticks = f.ticks;
      frame.settings = { ...settings, foamPaused: true };
      frame.time = 100;
      setFrame();
      f.update(frame, field, true);
      await b.sync();
      const paused = await f.inspect();
      const pauseExact = paused.density.every(
        (v: number, i: number) => v === beforePause[i],
      );
      const count = f.ticks;
      await b.capture(frame);
      await b.capture({ ...frame, width: 192, height: 144 });
      const captureTicks = f.ticks;
      const compareTicks = f.ticks;
      f.update(
        { ...frame, time: 101, compare: true },
        b.name === "WebGPU" ? b.original : b.originalTexture,
        true,
      );
      const comparePreserved = f.ticks === compareTicks;
      b.resetFoam();
      await b.capture(frame);
      const cleared = await f.inspect();
      const after = await b.readVolume();
      return {
        belowRoof,
        bankDepth,
        start,
        end,
        pauseExact,
        ticks,
        count,
        captureTicks,
        comparePreserved,
        cleared: cleared.density.every(
          (v: number, i: number) => i % 4 === 3 || v === 0,
        ),
        unchanged: original.every((v: number, i: number) => v === after[i]),
        finite:
          moving.density.every(Number.isFinite) &&
          moving.motion.every(Number.isFinite),
        glError: b.name === "WebGL2" ? b.gl.getError() : 0,
        diag: f.diagnostics(),
      };
    });
    expect(result.belowRoof).toBeCloseTo(2.65, 1);
    expect(result.bankDepth).toBe(0);
    expect(result.end.x - result.start.x).toBeGreaterThan(0.85);
    expect(result.end.x - result.start.x).toBeLessThan(1.1);
    expect(result.end.q / result.start.q).toBeCloseTo(Math.pow(0.5, 1 / 6), 1);
    expect(result.pauseExact).toBe(true);
    expect(result.count).toBe(result.ticks);
    expect(result.captureTicks).toBe(result.count);
    expect(result.comparePreserved).toBe(true);
    expect(result.cleared).toBe(true);
    expect(result.unchanged).toBe(true);
    expect(result.finite).toBe(true);
    expect(result.glError).toBe(0);
    expect(errors).toEqual([]);
  });

  test(`${renderer}: Ultra GPU markers and Cinematic spray/foam/bubbles render with correct empty-layer composition`, async ({
    page,
  }, info) => {
    const errors: string[] = [];
    page.on("pageerror", (e) => errors.push(e.message));
    page.on("console", (m) => {
      if (m.type() === "error") errors.push(m.text());
    });
    await ready(page, renderer);
    const result = await page.evaluate(async () => {
      const e = window.__frontier!,
        b: any = e.backend;
      const { packUniforms } = await import("/src/engine/uniforms.ts");
      const n = b.size;
      const volume = new Float32Array(n.x * n.y * n.z * 4);
      for (let z = 0; z < n.z; z++)
        for (let y = 0; y < n.y; y++)
          for (let x = 0; x < n.x; x++)
            volume[((z * n.y + y) * n.x + x) * 4] =
              -10 + ((y + 0.5) * 48) / n.y + 2;
      b.writeVolume(volume);
      const before = await b.readVolume();
      const base: any = {
        ...e.settings,
        foamBudget: "compact",
        foamView: "surface",
        water: true,
        waterFoam: 0.9,
        waterCurrent: 1.2,
        waterFlowMode: "directional",
        waterDirection: 0,
        waterReverse: false,
        wind: 0.8,
        foamSpray: 1,
        foamBubbles: 1,
        waterStreaks: 0,
      };
      const frame: any = {
        eye: [0, 8, 0],
        forward: [0, -1, 0],
        right: [1, 0, 0],
        up: [0, 0, -1],
        width: 192,
        height: 144,
        time: 0,
        brush: null,
        tool: "orbit",
        compare: false,
        settings: base,
      };
      const field = b.name === "WebGPU" ? b.fields[b.current] : b.texture;
      const set = () => {
        const values = packUniforms(frame, n);
        if (b.name === "WebGPU")
          b.device.queue.writeBuffer(b.uniform, 0, values);
        else {
          b.gl.bindBuffer(b.gl.UNIFORM_BUFFER, b.uniform);
          b.gl.bufferSubData(b.gl.UNIFORM_BUFFER, 0, values);
        }
      };
      const encode = async (blob: Blob) =>
        new Promise<string>((resolve) => {
          const r = new FileReader();
          r.onload = () => resolve((r.result as string).split(",")[1]);
          r.readAsDataURL(blob);
        });
      const tiers: any[] = [];
      for (const quality of ["ultra", "cinematic"]) {
        frame.settings = { ...base, foamQuality: quality };
        frame.time = 0;
        b.render(frame);
        await b.sync();
        for (let i = 1; i <= 20; i++) {
          frame.time = i / 30;
          set();
          b.foam.update(frame, field, true);
          if (i % 10 === 0) await b.sync();
        }
        await b.sync();
        const state = await b.foam.inspect();
        let q = 0;
        for (let i = 0; i < state.density.length; i += 4) q += state.density[i];
        const shot = await encode(await b.capture(frame));
        let optical = 0;
        if (quality === "cinematic") {
          for (const tex of [b.foam.air, b.foam.bubbles]) {
            const data = await b.foam.driver.read(tex);
            for (let i = 3; i < data.length; i += 4) optical += data[i];
          }
        }
        tiers.push({
          quality,
          counts: state.counts,
          q,
          optical,
          shot,
          finite: [
            state.positions,
            state.velocities,
            state.motion,
            state.density,
          ].every((a) => a.every(Number.isFinite)),
          diag: b.foam.diagnostics(),
        });
      }
      const images: string[] = [];
      for (const below of [false, true])
        for (const quality of ["low", "cinematic"]) {
          frame.eye = below ? [0, -0.5, 0] : [0, 8, 0];
          frame.forward = below ? [0, 1, 0] : [0, -1, 0];
          frame.settings = {
            ...base,
            foamQuality: quality,
            waterFoam: 0,
            waterCurrent: 0,
            wind: 0,
            foamPaused: true,
          };
          b.resetFoam();
          images.push(await encode(await b.capture(frame)));
        }
      const after = await b.readVolume();
      return {
        tiers,
        images,
        unchanged: before.every((v: number, i: number) => v === after[i]),
        glError: b.name === "WebGL2" ? b.gl.getError() : 0,
      };
    });
    for (const tier of result.tiers) {
      expect(tier.finite).toBe(true);
      expect(tier.q).toBeGreaterThan(0);
      expect(tier.counts[0]).toBeGreaterThan(20);
      expect(
        tier.counts.reduce((a: number, b: number) => a + b, 0),
      ).toBeLessThanOrEqual(tier.diag.particleCapacity);
      await info.attach(`${renderer}-${tier.quality}`, {
        body: Buffer.from(tier.shot, "base64"),
        contentType: "image/png",
      });
    }
    expect(result.tiers[0].counts.slice(1)).toEqual([0, 0]);
    expect(result.tiers[1].counts[1]).toBeGreaterThan(0);
    expect(result.tiers[1].counts[2]).toBeGreaterThan(0);
    expect(result.tiers[1].optical).toBeGreaterThan(0);
    expect(
      delta(
        Buffer.from(result.images[0], "base64"),
        Buffer.from(result.images[1], "base64"),
      ),
    ).toBeLessThan(1.2);
    expect(
      delta(
        Buffer.from(result.images[2], "base64"),
        Buffer.from(result.images[3], "base64"),
      ),
    ).toBeLessThan(1.2);
    expect(result.unchanged).toBe(true);
    expect(result.glError).toBe(0);
    expect(errors).toEqual([]);
  });
}

test("foam controls, diagnostics, project portability and compact mobile layout", async ({
  page,
}, info) => {
  await ready(page, "webgl");
  await expect(
    page.getByRole("button", { name: /Standard TRANSPORT/ }),
  ).toHaveAttribute("aria-pressed", "true");
  await page.getByLabel("Foam workload budget").selectOption("compact");
  await page.getByRole("button", { name: /Cinematic WHITEWATER/ }).click();
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.backend.render({ ...(e as any).makeFrame(), width: 320, height: 240 });
    await e.backend.sync();
  });
  await expect(page.getByLabel("Foam allocation")).toContainText(
    "48k particle slots",
  );
  await page.getByRole("button", { name: "Pause foam", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Resume foam", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  for (const name of [
    "density",
    "age",
    "velocity",
    "sources",
    "banks & bed",
    "surface",
  ]) {
    await page
      .getByRole("button", { name: `Foam ${name}`, exact: true })
      .click();
    await page.evaluate(async () => {
      const e = window.__frontier!;
      e.backend.render({ ...(e as any).makeFrame(), width: 192, height: 144 });
      await e.backend.sync();
    });
  }
  await page.getByRole("button", { name: "Restart foam", exact: true }).click();
  await page.getByRole("button", { name: "Export", exact: true }).click();
  const pending = page.waitForEvent("download");
  await page.getByRole("menuitem", { name: /SDF project/ }).click();
  const project = await pending;
  const bytes = await readFile((await project.path())!);
  await page.getByRole("button", { name: /Low LEGACY/ }).click();
  await page
    .locator('input[type="file"][accept*=".frontier"]')
    .setInputFiles({
      name: "foam.frontier",
      mimeType: "application/octet-stream",
      buffer: bytes,
    });
  await page
    .getByRole("dialog", { name: "Open this project?" })
    .getByRole("button", { name: "Continue", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: /Cinematic WHITEWATER/ }),
  ).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByLabel("Foam workload budget")).toHaveValue("compact");
  await page.waitForFunction(() => !window.__frontier!.busy);
  await page.evaluate(async () => {
    window.__frontier!.busy = true;
    await window.__frontier!.backend.sync();
  });
  // Engine load resets transient effects, not the saved parameter choices.
  await page.setViewportSize({ width: 390, height: 844 });
  await page.locator(".foam-controls").scrollIntoViewIfNeeded();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await page.screenshot({ path: info.outputPath("foam-mobile.png") });
});

test("missing float-target support retains a working, explicitly labelled Low fallback", async ({
  page,
}) => {
  await page.addInitScript(() => {
    const original = WebGL2RenderingContext.prototype.getExtension;
    WebGL2RenderingContext.prototype.getExtension = function (name: string) {
      return name === "EXT_color_buffer_float"
        ? null
        : original.call(this, name);
    };
  });
  await ready(page, "webgl");
  await expect(page.locator(".foam-warning")).toContainText("using Low");
  expect(
    await page.evaluate(() => window.__frontier!.backend.getDiagnostics().foam),
  ).toMatchObject({ supported: false, quality: "low", requested: "standard" });
});
