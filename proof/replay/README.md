# Replay the evidence

All new code here is an isolated experiment. The checkout's engine was not replaced. The gallery and frozen native passes work without the external repo. The input fixture is ~4.2 MiB compressed (hard links deduplicate identical inputs); it contains GLES shaders, named uniforms and **input** textures, no reference/output pixels. Only load trusted fixtures. Binary readers expect little-endian IEEE-754.

## Fastest check

Serve `proof/` and choose **Run a shader on your device**. The page compiles and executes the actual pass, not a screenshot. WebGL 2, float render targets, gzip `DecompressionStream` required. The separate path-tracer launch requires WebGPU in a secure context or localhost. `path-tracer/` is the supplied `Experimental/DeterministicPathTracer` studio at `c205cb69`, with its original meshes, WGSL and UI (standalone Serve.py omitted).

## Native C++ (Linux EGL/GLES 3, C++17)

From repository root:

```sh
ROOT=$PWD
R=$ROOT/proof/replay
D=$(mktemp -d)
mkdir "$D/native"
tar xzf "$R/native-inputs.tar.gz" -C "$D/native"
c++ -O2 -std=c++17 "$R/NativePass.cpp" -lEGL -lGLESv2 -o "$D/native-pass"
EGL_PLATFORM=surfaceless "$D/native-pass" "$D/native/packet" "$D/sun.rgba32f" 3
sha256sum "$D/sun.rgba32f"
EGL_PLATFORM=surfaceless python3 "$R/benchmark.py" "$D/native-pass" "$D/native" "$D"
```

Use the EGL platform appropriate to your GPU. On this CPU the Khronos EGL/GLES headers were used with the `libEGL.so`, `libGLESv2.so`, Vulkan and SwiftShader libraries from the Chromium 133 package; `ANGLE_DEFAULT_PLATFORM=swiftshader` and a library search path pointing there select software. Output is 480×270 RGBA FP32 readback of RGBA16F, bottom row first. Shadow R is key visibility; A is the harmonic blocker distance **as encoded by the lab shader**. `benchmark.json` holds same-driver digests. Cross-vendor float rounding is not guaranteed identical. Device-local timings do not include transfers or presentation.

## Regenerate from the pinned source

`prepare.sh` fetches the named source commit into a **new temporary directory**, unpacks just the two labs, applies the validation patch, compiles the C++ baker and regenerates `Assets/body.r8` (`cafb0fc49df76cef18cc2ecab358f4417b449692d404a1fe62bcdd819dfe0ee8`). The patch adds a one-shot capture entry point, instrumented readback, exact 480×270 targets, source isolation, sRGB output and unfiltered GI; it is not an engine patch. The capture harness uses a loopback-only file server. Keep builds, expanded fixtures and scratch outputs outside the checkout.

```sh
"$R/prepare.sh"       # set S to the printed scratch directory below
# In a scratch node project: npm install puppeteer-core@24.2.1
# Set NODE_PATH to that project's node_modules; CHROME to your local Chromium.
# Use a trusted test browser. For software Chromium, CHROME_ARGS is a JSON array,
# e.g. ["--use-gl=angle","--use-angle=swiftshader","--enable-unsafe-swiftshader"].
node "$R/capture.cjs" "$S/Experimental" "$D" "$R/jobs/lighting.json"
node "$R/capture.cjs" "$S/Experimental" "$D" "$R/jobs/native.json"
node "$R/capture.cjs" "$S/Experimental" "$D" "$R/jobs/motion.json"
node "$R/capture.cjs" "$S/Experimental" "$D" "$R/jobs/emission-motion.json"
# WebGPU run used Chromium 143.0.4, Vulkan/SwiftShader, --enable-unsafe-webgpu,
# --enable-features=Vulkan --use-vulkan=swiftshader --disable-vulkan-surface.
node "$R/capture.cjs" "$S/Experimental" "$D" pt
```

The job arrays are `[capture name, state overrides, raw plane or null]`; poses are absolute, not based on wall clock. The shadow/volume fixtures are pre-display floats. Building the additional native passes, references and images (Python 3, NumPy, Pillow, embreex):

```sh
ASSETS=$S/Experimental/ShadowAlgorithmLab/Assets
mkdir -p "$D/report/frames"
python3 "$R/packet_variant.py" "$D/native/native-cap" "$D/native/packet"
python3 "$R/lamp_variant.py" "$D/native/native-cap" "$D/native/lamps-fast" "$ASSETS/VehicleBenchmarkAssets.bin"
for name in native-local native-clip native-hybrid native-cap native-gi packet lamps-fast; do
  "$D/native-pass" "$D/native/$name" "$D/$name.cpp.raw" 1
done
cp "$D/packet.cpp.raw" "$D/packet.raw"; cp "$D/lamps-fast.cpp.raw" "$D/lamps-fast.raw"
export OPENBLAS_NUM_THREADS=1
python3 "$R/mesh_reference.py" "$D/native/native-cap" "$ASSETS/VehicleBenchmarkAssets.bin" "$D"
python3 "$R/floor_bounce.py" "$D/native/native-cap" "$ASSETS/VehicleBenchmarkAssets.bin" "$D/report"
python3 "$R/material_check.py" "$ASSETS/VehicleBenchmarkAssets.bin" "$ASSETS/body.r8" "$D/report/frames"
python3 "$R/slices.py" "$ASSETS/VehicleBenchmarkAssets.bin" "$D/report/frames"
python3 "$R/report.py" "$D" "$D/report"
```

`report.py` checks raw finiteness, zero-source behaviour, repeat hashes, native pass parity, changed motion frames and image dimensions. It compares against independent triangle references, not an AI-generated rendering or a filtered image. Bit parity assertions are for the tested software driver. Proof PNGs and animations are under `../frames/`.

For a quick check of the published evidence without installing rendering dependencies: `python3 proof/replay/verify.py`. The Python reference runs used `numpy==2.4.6`, `pillow==13.0.0`, `embreex==4.4.0.post1`. C++ compilation used GCC 12.2.0. The raw readbacks are scratch products; lossless PNGs, source manifests, hashes, native inputs and checks are committed instead of hundreds of MB of duplicate float payloads.
