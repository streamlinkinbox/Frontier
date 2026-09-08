import { test, expect } from "@playwright/test";
import { PNG } from "pngjs";

async function ready(page: import("@playwright/test").Page) {
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  expect(await page.locator(".has-error").count()).toBe(0);
}

test("camera modes never change from incidental input and live status is independent", async ({
  page,
}) => {
  await ready(page);
  const canvas = page.locator(".viewport-canvas"),
    box = (await canvas.boundingBox())!;
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.5);
  const initial = await page.evaluate(() =>
    window.__frontier!.camera.basis(1.5),
  );
  await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.5);
  await page.keyboard.down("w");
  await page.waitForTimeout(180);
  await page.keyboard.up("w");
  expect(await page.evaluate(() => window.__frontier!.camera.mode)).toBe(
    "orbit",
  );
  expect(
    await page.evaluate(() => window.__frontier!.camera.basis(1.5)),
  ).toEqual(initial);
  await page.mouse.down({ button: "right" });
  await page.mouse.move(box.x + box.width * 0.55, box.y + box.height * 0.53, {
    steps: 5,
  });
  await page.mouse.up({ button: "right" });
  expect(await page.evaluate(() => window.__frontier!.camera.mode)).toBe(
    "orbit",
  );
  const pose = await page.evaluate(
    () => window.__frontier!.camera.basis(1.5).eye,
  );
  await page.getByRole("button", { name: "Camera controls" }).click();
  await page.getByRole("menuitem", { name: /^Fly camera/ }).click();
  expect(
    await page.evaluate(() => window.__frontier!.camera.basis(1.5).eye),
  ).toEqual(pose);
  await expect(page.locator(".viewport-status")).toContainText("Live viewport");
  await expect(page.locator(".viewport-help")).toContainText("FLY");
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.5);
  const fly = await page.evaluate(() => ({
    p: window.__frontier!.camera.position.slice(),
    yaw: window.__frontier!.camera.yaw,
    speed: window.__frontier!.camera.speed,
  }));
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.6, {
    steps: 5,
  });
  await page.mouse.up();
  expect(await page.evaluate(() => window.__frontier!.camera.position)).toEqual(
    fly.p,
  );
  expect(await page.evaluate(() => window.__frontier!.camera.yaw)).toBe(
    fly.yaw,
  );
  await page.mouse.wheel(0, -180);
  await expect
    .poll(() => page.evaluate(() => window.__frontier!.camera.speed))
    .toBeGreaterThan(fly.speed);
  expect(await page.evaluate(() => window.__frontier!.camera.position)).toEqual(
    fly.p,
  );
  await page
    .getByRole("button", { name: "Frame terrain", exact: true })
    .click();
  expect(await page.evaluate(() => window.__frontier!.camera.mode)).toBe("fly");
  await page.getByRole("button", { name: "Camera controls" }).click();
  await page.getByRole("menuitem", { name: /^Orbit camera/ }).click();
  expect(await page.evaluate(() => window.__frontier!.camera.mode)).toBe(
    "orbit",
  );
});

test("layered rock relief changes lighting, not albedo settings or saved voxels", async ({
  page,
}, testInfo) => {
  await ready(page);
  await page.getByRole("tab", { name: "Materials", exact: true }).click();
  await page.getByRole("button", { name: "Legacy rock", exact: true }).click();
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.pause();
    e.busy = true;
    await e.backend.sync();
    const hit = await e.backend.pick((e as any).makeFrame(), 0.3, 0.05);
    if (!hit) throw new Error("No material face");
    e.camera.lookAt(
      hit.position.map((v, i) => v + hit.normal[i] * 2) as [
        number,
        number,
        number,
      ],
      hit.position,
    );
    (window as any).detailVolume = await e.backend.readVolume();
    e.update({
      ...e.settings,
      water: false,
      materialWeathering: 0,
      materialBedding: 0,
      rockRelief: 0,
      rockLayerRelief: 0,
    });
    e.backend.render({ ...(e as any).makeFrame(), time: 0, brush: null });
    await e.backend.sync();
  });
  const flat = await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("rock-without-relief.png") });
  const color = await page.evaluate(
    () => window.__frontier!.settings.materialColor,
  );
  await page.getByRole("button", { name: "Layered", exact: true }).click();
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.backend.render({ ...(e as any).makeFrame(), time: 0, brush: null });
    await e.backend.sync();
  });
  const relief = await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("layered-rock-relief.png") });
  const a = PNG.sync.read(flat),
    b = PNG.sync.read(relief);
  let changed = 0;
  for (let i = 0; i < a.data.length; i += 4)
    if (
      Math.abs(a.data[i] - b.data[i]) +
        Math.abs(a.data[i + 1] - b.data[i + 1]) +
        Math.abs(a.data[i + 2] - b.data[i + 2]) >
      15
    )
      changed++;
  expect(changed / (a.width * a.height)).toBeGreaterThan(0.1);
  expect(
    await page.evaluate(() => window.__frontier!.settings.materialColor),
  ).toBe(color);
  expect(
    await page.evaluate(async () => {
      const data = await window.__frontier!.backend.readVolume();
      return data.every((v, i) => v === (window as any).detailVolume[i]);
    }),
  ).toBe(true);
});

test("river route controls expose downstream/reverse flow without changing terrain", async ({
  page,
}, testInfo) => {
  await ready(page);
  await page.getByRole("tab", { name: "Environment", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Follow canyon", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await page
    .getByRole("switch", { name: "Reverse downstream current" })
    .click();
  expect(
    await page.evaluate(() => window.__frontier!.settings.waterReverse),
  ).toBe(true);
  await expect(page.locator(".downstream-label")).toContainText("−Z → +Z");
  await page
    .getByRole("switch", { name: "Reverse downstream current" })
    .click();
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.pause();
    e.busy = true;
    await e.backend.sync();
    e.camera.lookAt([-9, 5.2, 29], [-1, 3.7, -5]);
    e.update({
      ...e.settings,
      waterLevel: 2.65,
      waterCurrent: 1,
      waterStreaks: 0.8,
      wind: 0.2,
    });
    e.backend.render({ ...(e as any).makeFrame(), time: 1, brush: null });
    await e.backend.sync();
  });
  await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("downstream-river.png") });
  await page
    .getByRole("button", { name: "Compass heading", exact: true })
    .click();
  await expect(
    page.getByRole("slider", { name: "Flow direction", exact: true }),
  ).toBeEnabled();
  expect(await page.evaluate(() => window.__frontier!.steps)).toBe(0);
});

test("shader layered relief and channel arc coordinates match their CPU references", async ({
  page,
}) => {
  await ready(page);
  const result = await page.evaluate(async () => {
    const [{ glFragment, glVertex }, { layeredRock }, { riverCoordinates }] =
      await Promise.all([
        import("/src/engine/shaders.ts"),
        import("/src/engine/surfaceDetail.ts"),
        import("/src/engine/flowRoute.ts"),
      ]);
    const e = window.__frontier!;
    e.busy = true;
    await e.backend.sync();
    e.backend.render({ ...(e as any).makeFrame(), time: 2, brush: null });
    await e.backend.sync();
    const gl = (e.backend as any).gl as WebGL2RenderingContext;
    const compile = (kind: number, source: string) => {
      const sh = gl.createShader(kind)!;
      gl.shaderSource(sh, source);
      gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS))
        throw new Error(gl.getShaderInfoLog(sh) || "shader");
      return sh;
    };
    const probe = (body: string) => {
      const source = glFragment.replace(
        /void main\(\)\{fragColor=renderPixel\([^\n]+\);\}/,
        `void main(){${body}}`,
      );
      const vs = compile(gl.VERTEX_SHADER, glVertex),
        fs = compile(gl.FRAGMENT_SHADER, source),
        program = gl.createProgram()!;
      gl.attachShader(program, vs);
      gl.attachShader(program, fs);
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS))
        throw new Error(gl.getProgramInfoLog(program) || "link");
      gl.useProgram(program);
      gl.uniformBlockBinding(
        program,
        gl.getUniformBlockIndex(program, "Params"),
        0,
      );
      const texture = gl.createTexture()!,
        framebuffer = gl.createFramebuffer()!;
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA8,
        1,
        1,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        null,
      );
      gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
      gl.framebufferTexture2D(
        gl.FRAMEBUFFER,
        gl.COLOR_ATTACHMENT0,
        gl.TEXTURE_2D,
        texture,
        0,
      );
      gl.viewport(0, 0, 1, 1);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      const pixels = new Uint8Array(4);
      gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.deleteFramebuffer(framebuffer);
      gl.deleteTexture(texture);
      gl.deleteProgram(program);
      gl.deleteShader(vs);
      gl.deleteShader(fs);
      return [...pixels].map((v) => v / 255);
    };
    const p: [number, number, number] = [3.71, 8.43, -4.12],
      height = layeredRock(p, e.settings),
      route = riverCoordinates(p, e.settings);
    const gpuHeight = probe(
      "vec4 h=layeredRock(vec3(3.71,8.43,-4.12),0.);fragColor=vec4(h.x/.2+.5,h.y*.15+.5,h.z*.15+.5,h.w*.15+.5);",
    );
    const gpuRoute = probe(
      "vec4 p=flowCoordinates(vec3(3.71,8.43,-4.12));fragColor=vec4(p.x/256.+.5,p.y/128.+.5,p.z/4.+.5,p.w/4.+.5);",
    );
    return {
      gpuHeight,
      expectedHeight: [
        height[0] / 0.2 + 0.5,
        ...height.slice(1).map((v) => v * 0.15 + 0.5),
      ],
      gpuRoute,
      expectedRoute: [
        route.along / 256 + 0.5,
        route.across / 128 + 0.5,
        route.du[1] / 4 + 0.5,
        route.dv[1] / 4 + 0.5,
      ],
    };
  });
  for (let i = 0; i < 4; i++) {
    expect(
      Math.abs(result.gpuHeight[i] - result.expectedHeight[i]),
    ).toBeLessThan(0.018);
    expect(Math.abs(result.gpuRoute[i] - result.expectedRoute[i])).toBeLessThan(
      0.008,
    );
  }
});
