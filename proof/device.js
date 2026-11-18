// User-initiated GPU check: only shader inputs are in the archive; no output image is sampled.
const run = document.querySelector("#execute");
const status = document.querySelector("#gpu-status");
let members;
async function inputs() {
  if (members) return members;
  const r = await fetch("replay/native-inputs.tar.gz");
  if (!r.ok) throw Error("Cannot read the GPU input archive");
  const data = new Uint8Array(
    await new Response(
      r.body.pipeThrough(new DecompressionStream("gzip")),
    ).arrayBuffer(),
  );
  const text = new TextDecoder(),
    files = new Map();
  for (let off = 0; off + 512 <= data.length && data[off];) {
    const field = (start, len) =>
      text.decode(data.subarray(off + start, off + start + len)).split("\0")[0];
    const name = field(0, 100),
      size = parseInt(field(124, 12), 8);
    files.set(
      name,
      field(156, 1) === "1"
        ? files.get(field(157, 100))
        : data.subarray(off + 512, off + 512 + size),
    );
    off += 512 + Math.ceil(size / 512) * 512;
  }
  members = files;
  return files;
}
run.onclick = async () => {
  run.disabled = true;
  let gl;
  try {
    status.textContent = "Loading pass inputs…";
    const files = await inputs(),
      mode = document.querySelector("#pass").value;
    const get = (name) => files.get(mode + "/" + name),
      txt = (name) => new TextDecoder().decode(get(name));
    const canvas = document.createElement("canvas");
    canvas.width = 480;
    canvas.height = 270;
    gl = canvas.getContext("webgl2", { antialias: false });
    if (!gl?.getExtension("EXT_color_buffer_float"))
      throw Error("WebGL 2 and floating-point render targets are required");
    const p = gl.createProgram();
    for (const [type, name] of [
      [gl.VERTEX_SHADER, "pass.vert"],
      [gl.FRAGMENT_SHADER, "pass.frag"],
    ]) {
      const sh = gl.createShader(type);
      gl.shaderSource(sh, txt(name));
      gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS))
        throw Error(gl.getShaderInfoLog(sh));
      gl.attachShader(p, sh);
    }
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS))
      throw Error(gl.getProgramInfoLog(p));
    gl.useProgram(p);
    const out = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, out);
    gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA16F, 480, 270);
    const fb = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
    gl.framebufferTexture2D(
      gl.FRAMEBUFFER,
      gl.COLOR_ATTACHMENT0,
      gl.TEXTURE_2D,
      out,
      0,
    );
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
      throw Error("Incomplete pass target");
    for (const line of txt("bindings").trim().split("\n")) {
      const [kind, name, ...tokens] = line.split(" "),
        nums = tokens.map(Number),
        loc = gl.getUniformLocation(p, name);
      if (kind === "T") {
        const [unit, target, internal, w, h, d, fmt, filter] = nums;
        const buf = get(name + ".f32");
        const v = new Float32Array(
          buf.buffer,
          buf.byteOffset,
          buf.byteLength / 4,
        );
        gl.activeTexture(gl.TEXTURE0 + unit);
        gl.bindTexture(target, gl.createTexture());
        if (target === gl.TEXTURE_3D)
          gl.texImage3D(target, 0, internal, w, h, d, 0, fmt, gl.FLOAT, v);
        else gl.texImage2D(target, 0, internal, w, h, 0, fmt, gl.FLOAT, v);
        for (const par of [gl.TEXTURE_MIN_FILTER, gl.TEXTURE_MAG_FILTER])
          gl.texParameteri(target, par, filter);
        for (const par of [
          gl.TEXTURE_WRAP_S,
          gl.TEXTURE_WRAP_T,
          gl.TEXTURE_WRAP_R,
        ])
          gl.texParameteri(target, par, gl.CLAMP_TO_EDGE);
        gl.uniform1i(loc, unit);
      } else {
        const [type, ...v] = nums;
        if (type === gl.INT || type === gl.BOOL) gl.uniform1i(loc, v[0]);
        else if (type === gl.FLOAT) gl.uniform1f(loc, v[0]);
        else if (type === gl.FLOAT_VEC2) gl.uniform2fv(loc, v);
        else if (type === gl.FLOAT_VEC3) gl.uniform3fv(loc, v);
        else if (type === gl.FLOAT_MAT4) gl.uniformMatrix4fv(loc, false, v);
        else throw Error("Unknown uniform: " + name);
      }
    }
    const err = gl.getError();
    if (err) throw Error("Pass setup: GL " + err);
    gl.viewport(0, 0, 480, 270);
    const t = performance.now();
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    const a = new Float32Array(480 * 270 * 4);
    gl.readPixels(0, 0, 480, 270, gl.RGBA, gl.FLOAT, a);
    const elapsed = performance.now() - t;
    if (gl.getError() || !a.every(Number.isFinite))
      throw Error("Invalid pass output");
    const ctx = document.querySelector("#device").getContext("2d"),
      img = ctx.createImageData(480, 270),
      light = ["native-gi", "lamps-fast"].includes(mode);
    for (let y = 0; y < 270; y++)
      for (let x = 0; x < 480; x++) {
        let i = (y * 480 + x) * 4,
          j = ((269 - y) * 480 + x) * 4;
        for (let k = 0; k < 3; k++) {
          let v = a[j + (light ? k : 0)];
          if (light) {
            v *= 4;
            v = v / (1 + v);
            v =
              v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
          }
          img.data[i + k] = 255 * v;
        }
        img.data[i + 3] = 255;
      }
    ctx.putImageData(img, 0, 0);
    let digest = "";
    if (crypto.subtle) {
      const bytes = await crypto.subtle.digest("SHA-256", a.buffer);
      digest = Array.from(new Uint8Array(bytes), (n) =>
        n.toString(16).padStart(2, "0"),
      ).join("");
    }
    status.textContent = `Executed on your WebGL implementation. ${elapsed.toFixed(1)} ms including blocking readback (not FPS). Linear RGBA digest: ${digest || "unavailable"}`;
  } catch (e) {
    status.textContent = "Could not execute this device check: " + e.message;
  } finally {
    gl?.getExtension("WEBGL_lose_context")?.loseContext();
    run.disabled = false;
  }
};
