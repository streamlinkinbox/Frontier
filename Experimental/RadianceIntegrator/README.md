# Radiance Transport — usage

A standalone native-WebGPU triangle radiance-cascades demo. Open `index.html` through an HTTPS static host,
or serve the repository root locally and open `/Frontier/Experimental/RadianceIntegrator/index.html`.
There is no build step, editor workspace or WebGL fallback. Use a WebGPU-capable browser/device.

## Input

- Left drag: orbit. Right drag: pan. Wheel: zoom.
- Space: pause/resume the lighting clock. H: show/hide controls.
- Reset view: restore the starting camera.
- Quality preset: Fast, Balanced or High; adjusting quality sliders can select Custom automatically.
- Indirect only: show the cascade contribution, including directly received triangle emission/environment.
- Merged atlas / transmittance: inspect the cascade selected by the Debug cascade slider.
- Point-light proxies off and sky zero: isolate actual emissive-triangle lighting.
- Ray-work counters: measure traversal workload; atomic instrumentation adds overhead.

For performance, reduce render scale, increase probe spacing, reduce angular width or use smaller shadow maps.
The internal resolution is bounded to 960 by 720. The software-adapter warning is intentional; software timings
are not representative of a gaming GPU. GPU data is an allocation estimate, not a driver memory query.

## Documentation and checks

The [technical review](../../Docs/RadianceIntegratorReview.html) covers the algorithm, source references,
executed tests, measured software timings, approximations and open-world development plan.
The same review is available [as Markdown](../../Docs/RadianceIntegratorReview.md).

From the repository root:

```sh
node VisualProof/RadianceIntegrator/VerifyStructure.mjs
```

That writes the CPU reference rays and structural report into ignored `_AgentScratch/TransportProof/`.
The browser verifier requires Playwright and Sparticuz Chromium; install them into ignored scratch, not this demo:

```sh
npm install --prefix _AgentScratch/build/WebGpuStudy --no-save playwright@1.63.0 @sparticuz/chromium@133.0.0
NODE_PATH="$PWD/_AgentScratch/build/WebGpuStudy/node_modules" node VisualProof/RadianceIntegrator/VerifyWebGpu.cjs
```

Serve the repository on port 8080 before running the browser verifier. `TRANSPORT_URL` overrides its default URL;
`PREVIEW_CHROMIUM` and `PREVIEW_LIBRARIES` can override the executable and library directory. The default test
launcher targets Linux SwiftShader and `/tmp/vk_swiftshader_icd.json`. Hardware browser validation requires an
appropriate hardware launch configuration instead; the default verifier is not a hardware benchmark.

`?manual=1&width=640` disables the animation loop and fixes the requested internal width for controlled captures.
`window.TransportApp.Step(time)` renders a specified finite time and waits for GPU completion. `Readback()` reads
linear HDR history, while `ReadCascade(level)` reads the merged directional buffer. This is a verification API,
not a scene-file format. The normal hosted URL does not need these parameters.
