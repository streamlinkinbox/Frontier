# Glinty 2025 versus legacy cellular

Two actual matched renders, **left to right: sand / blue metallic car paint / snow**. Cellular remains unchanged and available; this does **not** switch the production default. The Glinty side uses an author-demo-derived mirror, not an authenticated complete author reference implementation.

| Method | Visual observation | Three-light software cost rank |
|---|---|---|
| Legacy cellular | Broader visible flakes; paint glints concentrated around highlights | 🥇 Lower |
| Glinty-derived NDF | Smaller distributed sparkles; distinct blue-paint sparkle field | 🥈 Higher |

## Two renders

**Legacy cellular**

![Legacy cellular: sand, blue metallic paint, snow](Images/GlintPairCellular.png)

**Glinty-derived**

![Glinty-derived: sand, blue metallic paint, snow](Images/GlintPairGlinty.png)

Same camera, three sample spheres, three lights, exposure, GGX/Smith/Schlick wrapper and paint clearcoat. These are material samples, **not** a sand simulation, snow transport model or complete vehicle render. Different material parameters/models are not equal-quality results: cellular perturbs albedo/roughness/normal once per pixel; Glinty evaluates an NDF per light, mixed 70% with 30% smooth GGX. Exact settings and hashes are in the reports.

## Measured shading cost

**Device:** Linux, Intel Xeon 2.60 GHz; Chromium 143.0.7499.0, ANGLE Vulkan **SwiftShader CPU software renderer**. No physical RTX/GTX or Windows execution. Numbers are elapsed queries for one shaded draw, **not PC frame times or FPS predictions**.

| Resolution / lights / density multiplier | Smooth p50/p95 ms | Cellular p50/p95 ms | Glinty p50/p95 ms |
|---|---:|---:|---:|
| 768×384 / 3 / 1× — pictured | 16.87 / 21.44 | 138.50 / 152.52 | 273.51 / 289.24 |
| 256×128 / 1 / 1× | 1.52 / 2.94 | 15.21 / 16.77 | 11.84 / 14.43 |
| 256×128 / 3 / 1× | 1.81 / 2.24 | 16.60 / 16.94 | 28.78 / 34.09 |
| 768×384 / 3 / 10× | 16.09 / 18.31 | 151.01 / 174.46 | 281.44 / 368.56 |

- Two warmups, nine samples/method/case, rotating method order; p95 is nearest-rank, therefore the maximum of nine samples. Timer availability/disjoint, GL errors, finite/nonnegative RGB and alpha=1 checked.
- Common stencil coverage rejects empty background while retaining derivative helper lanes. Coverage setup, shader compilation and readback are excluded from timestamps; this is **not full-frame renderer cost**.
- Independent browser repeat: pictured cellular **137.94 ms**, Glinty **250.19 ms** p50. Both captures reproduce **byte-identical RGB hashes**. The separate repeat report preserves timing variability rather than hiding it.
- Glinty is ~22% faster in the small one-light case, but ~1.97× slower in the pictured three-light case. This fixture does **not** establish a general speedup.
- Existing cellular visits 27 neighbours/layer (paint: three layers, 81 visits), plus filtering/noise. The mirror evaluates 2 LOD × 4 angular × 4 spatial contributions per light: 32, or 96 for three lights. Constant work with particle density does not mean negligible work or no loops.

## Resources

| Resource | Cellular | Glinty |
|---|---:|---:|
| Feature texture/buffer allocations | 0 B | 0 B |
| Particle list / dictionary | 0 B / 0 B | 0 B / 0 B |
| Isolated browser host PSS, post-run | 187.18 MiB | 176.21 MiB |
| Physical VRAM peak / register spills | Not measured | Not measured |

Shared: RGBA32F target **4.5 MiB**, depth/stencil **1.125 MiB**, CPU float readback **4.5 MiB**. PSS includes the entire browser, driver, JS and retained outputs; **not feature-only memory, VRAM or peak allocation**. Isolated smooth PSS: 158.85 MiB. The engine's 272-byte cellular descriptor is not allocated as a uniform buffer here; compile-time fixture settings are used.

## Provenance and permission limits

- Author page <https://thonatt.github.io/> links the Glinty 2025 demo <https://www.shadertoy.com/view/tcdGDl> (*Glint material*, msqrt). Paper: *Evaluating and Sampling Glinty NDFs in Constant Time*, DOI `10.1145/3763282`.
- Full GLSL mirror: `d-k-ivanov/d-k-ivanov.github.io`, revision `96384ebf29c7471f3cda15ec2d53ad3d94ec0fb7`, `shaders/assets/shaders/sampling/glinty_ndf.fragment.glsl`, blob `c161b097d785346c773ebcd0045003a8896f431e`. Public demo prefix matches; **full-file identity is unverified**.
- NDF helpers are extracted without algorithm changes; float `f` suffixes are removed for GLSL ES. Scene geometry/lighting are replaced. Spherical UVs and bounded same-sphere ray differentials avoid cross-silhouette singular covariance; this wrapper regularization limits grazing/polar behaviour and is not production filtering validation.
- Mirror licence endpoint returned 404. [Shadertoy terms](https://www.shadertoy.com/terms) specify default CC BY-NC-SA 3.0 absent another author licence. **Commercial engine reuse is not established.** External shader source remains ignored scratch data, not vendored production code. No production-default switch is justified.

## Reproduce and continue

```bash
# Existing GitHub authentication; pinned browser dependencies in ignored scratch:
npm install --prefix _ClaudeScratch/tmp/GlintTools puppeteer-core@24.37.2 @sparticuz/chromium@143.0.4
# On this Linux image, extract bundled al2023.tar.br libraries to the path used by the runner:
python3 - <<'PY'
import pathlib, subprocess
p = pathlib.Path('_ClaudeScratch/tmp/GlintTools/node_modules/@sparticuz/chromium/bin/al2023.tar.br')
d = pathlib.Path('_ClaudeScratch/tmp/NativeBrowser'); d.mkdir(parents=True, exist_ok=True)
raw = subprocess.check_output(['node', '-e', 'process.stdout.write(require("zlib").brotliDecompressSync(require("fs").readFileSync(process.argv[1])))', str(p)])
subprocess.run(['tar', '-xf', '-', '-C', str(d)], input=raw, check=True)
PY
python3 Exhibits/TextureStudio/Code/PrepareGlintPair.py
node Exhibits/TextureStudio/Code/VerifyGlintPair.mjs
node Exhibits/TextureStudio/Code/VerifyGlintPair.mjs --repeat
for M in Smooth Cellular Glinty; do node Exhibits/TextureStudio/Code/VerifyGlintPair.mjs --isolate=$M; done
```

Reports: [primary](Results/GlintPair.json), [repeat](Results/GlintPairRepeat.json), isolated host memory [smooth](Results/GlintPairMemorySmooth.json), [cellular](Results/GlintPairMemoryCellular.json), [Glinty](Results/GlintPairMemoryGlinty.json). Exactly two PNG captures; repeat/intermediate raw images stay ignored.

The [historical native diagnostic](MaterialEnergy.md) found three white-furnace excesses; the subsequent [scoped native/Vulkan energy correction](MaterialEnergyAcceptance.md) now corrects those fixtures without replacing the material library. Selected layered scene integration is [rendered](../LightCompute/MaterialScene.md); full material-option coverage, temporal/filtering checks, sampling/PDF validation, licensing clearance for third-party glint research and physical-GPU performance remain unfinished.
