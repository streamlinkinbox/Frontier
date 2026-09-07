import { test, expect } from "@playwright/test";
import { PNG } from "pngjs";
const difference = (a: Buffer, b: Buffer) => {
  const x = PNG.sync.read(a),
    y = PNG.sync.read(b);
  let changed = 0;
  for (let i = 0; i < x.data.length; i += 4)
    if (
      Math.abs(x.data[i] - y.data[i]) +
        Math.abs(x.data[i + 1] - y.data[i + 1]) +
        Math.abs(x.data[i + 2] - y.data[i + 2]) >
      18
    )
      changed++;
  return changed / (x.width * x.height);
};

test("four editable material structures render on the same unmodified WebGL volume", async ({
  page,
}, testInfo) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  expect(
    await page.locator(".has-error").count(),
    "Renderer startup must succeed",
  ).toBe(0);
  await page.getByRole("tab", { name: "Materials", exact: true }).click();
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.pause();
    e.busy = true;
    await e.backend.sync();
    const frame = (e as any).makeFrame();
    const hit = await e.backend.pick(frame, 0.3, 0.05);
    if (!hit) throw new Error("No specimen surface found");
    e.camera.lookAt(
      hit.position.map((v, i) => v + hit.normal[i] * 1.4) as [
        number,
        number,
        number,
      ],
      hit.position,
    );
    (window as any).materialBefore = await e.backend.readVolume();
  });
  const images: Buffer[] = [];
  for (const material of ["Sandstone", "Limestone", "Granite", "Basalt"]) {
    await page
      .locator(".material-presets")
      .getByRole("button", { name: new RegExp(`^${material}`) })
      .click();
    await page.evaluate(async () => {
      const e = window.__frontier!;
      e.backend.render({ ...(e as any).makeFrame(), time: 0, brush: null });
      await e.backend.sync();
    });
    images.push(
      await page.locator(".viewport-canvas").screenshot({
        path: testInfo.outputPath(`${material.toLowerCase()}.png`),
      }),
    );
    const data = PNG.sync.read(images.at(-1)!);
    expect(data.data.some((v, i) => i % 4 !== 3 && v > 50)).toBe(true);
  }
  for (let i = 1; i < images.length; i++)
    expect(difference(images[i - 1], images[i])).toBeGreaterThan(0.12);
  await page
    .getByRole("slider", { name: "Surface roughness", exact: true })
    .focus();
  await page.keyboard.press("Home");
  await page
    .getByRole("slider", { name: "Surface moisture", exact: true })
    .focus();
  await page.keyboard.press("End");
  await page
    .getByRole("slider", { name: "Refractive index", exact: true })
    .focus();
  await page.keyboard.press("End");
  expect(
    await page.evaluate(() => ({
      rough: window.__frontier!.settings.materialRoughness,
      moist: window.__frontier!.settings.materialMoisture,
      ior: window.__frontier!.settings.materialIOR,
    })),
  ).toEqual({ rough: 0.12, moist: 1, ior: 1.8 });
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.backend.render({ ...(e as any).makeFrame(), time: 0, brush: null });
    await e.backend.sync();
  });
  const wet = await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("wet-basalt.png") });
  expect(difference(images[3], wet)).toBeGreaterThan(0.02);
  expect(
    await page.evaluate(async () => {
      const data = await window.__frontier!.backend.readVolume();
      return data.every((v, i) => v === (window as any).materialBefore[i]);
    }),
  ).toBe(true);
  await page.getByRole("button", { name: "Reset material parameters" }).click();
  expect(
    await page.evaluate(() => window.__frontier!.settings.materialRoughness),
  ).toBe(0.73);
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    390,
  );
  await expect(
    page.getByRole("slider", { name: "Surface roughness", exact: true }),
  ).toBeVisible();
  expect(errors).toEqual([]);
});

test("the actual WebGL refraction entry ray does not march through a nearby bank", async ({
  page,
}) => {
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  const probe = await page.evaluate(async () => {
    const [{ glFragment, glVertex }] = await Promise.all([
      import("/src/engine/shaders.ts"),
    ]);
    const e = window.__frontier!;
    e.busy = true;
    await e.backend.sync();
    const size = e.backend.size,
      data = new Float32Array(size.x * size.y * size.z * 4),
      h = 96 / size.x;
    for (let z = 0; z < size.z; z++)
      for (let y = 0; y < size.y; y++)
        for (let x = 0; x < size.x; x++)
          data[((z * size.y + y) * size.x + x) * 4] =
            -10 + (y + 0.5) * h - 5.38;
    e.backend.writeVolume(data);
    e.backend.render({ ...(e as any).makeFrame(), time: 0 });
    await e.backend.sync();
    const gl = (e.backend as any).gl as WebGL2RenderingContext;
    const fragment = glFragment.replace(
      /void main\(\)\{fragColor=renderPixel\([^\n]+\);\}/,
      `void main(){
      vec3 p=vec3(0.,5.4,0.);vec3 direction=vec3(0.,-1.,0.);
      vec3 origin=waterRayOrigin(p,direction);vec2 hit=traceWaterRay(origin,direction);
      fragColor=vec4(clamp(hit.x,0.,1.),hit.y>.5?1.:0.,map(origin)>0.?1.:0.,1.);
    }`,
    );
    const compile = (type: number, code: string) => {
      const shader = gl.createShader(type)!;
      gl.shaderSource(shader, code);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
        throw new Error(gl.getShaderInfoLog(shader) || "compile");
      return shader;
    };
    const vertex = compile(gl.VERTEX_SHADER, glVertex),
      pixel = compile(gl.FRAGMENT_SHADER, fragment),
      program = gl.createProgram()!;
    gl.attachShader(program, vertex);
    gl.attachShader(program, pixel);
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      throw new Error(gl.getProgramInfoLog(program) || "link");
    gl.useProgram(program);
    gl.uniformBlockBinding(
      program,
      gl.getUniformBlockIndex(program, "Params"),
      0,
    );
    gl.uniform1i(gl.getUniformLocation(program, "field"), 0);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, 1, 1);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    const out = new Uint8Array(4);
    gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, out);
    gl.deleteProgram(program);
    gl.deleteShader(vertex);
    gl.deleteShader(pixel);
    return [...out];
  });
  expect(probe[0]).toBeLessThan(15); // Entry distance < 6 cm, not a many-meter far exit.
  expect(probe[1]).toBe(255); // Found first rock entry.
  expect(probe[2]).toBe(255); // Safe ray origin remains outside rock.
});

test("raised water and an overhang render above and below without shader errors", async ({
  page,
}, testInfo) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.busy = true;
    await e.backend.sync();
    e.update({
      ...e.settings,
      waterLevel: 5.4,
      wind: 0.78,
      waterClarity: 1,
      detail: 1.2,
    });
  });
  for (const [name, eye] of [
    ["above-overhang", [-8, 6.2, 19]],
    ["below-overhang", [-8, 4.9, 19]],
  ] as const) {
    await page.evaluate(async (eye) => {
      const e = window.__frontier!;
      e.camera.lookAt([...eye], [-12, 5.4, 8]);
      e.backend.render({ ...(e as any).makeFrame(), time: 2, brush: null });
      await e.backend.sync();
    }, eye);
    const bytes = await page
      .locator(".viewport-canvas")
      .screenshot({ path: testInfo.outputPath(`${name}.png`) });
    const png = PNG.sync.read(bytes);
    expect(png.data.some((v, i) => i % 4 !== 3 && v > 45)).toBe(true);
  }
  expect(errors).toEqual([]);
});
