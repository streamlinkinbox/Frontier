# Pond — Performance & Shader Fixes (applied 2026-09-21)

This complements `docs/VOLUMETRIC-FLUID-AAA-2026.md`. It is the **actionable patch log** for the existing 2D heightfield pond (`solver.mjs` + `water-materials.mjs` + `app.js`). The volumetric AAA stack is separate.

## What was wrong (tiling)

Before this branch, `water-materials.mjs` used:

```glsl
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){ /* bilinear of hash */ }
vec2 micro=vec2(cos(p.x*8.1+p.y*3.7+uTime*1.7)+.42*cos(p.x*19.-p.y*11.+uTime*2.3), …);
float caustic=pow(abs(sin(bed.x*5.+sin(bed.z*3.+))*sin(bed.z*5.+…)),14.);
float film=smoothstep(.48,.72,noise(p*2.5+sin(p.yx*1.7)*.3))*uFilm;
```

All single-frequency, axis-aligned, sin-periodic → visible **≈4 m repeat**, especially in swamp/ocean at grazing angle and on puddle bed.

## Fix shipped in `water-materials.mjs`

- `hash12` with `mod289` permutation (IQ hash) — removes sin periodicity at large `p` (important for 20 m ponds).
- `valueNoise` bilinear of `hash12` + `fbm` 3 octaves with **per-octave rotation 23°** (`rot23`) and frequency 1×,2.07×,4.28× — standard anti-tiling.
- `fbmWarp` domain warping (`q = fbm(p)`, `r = fbm(p+3.7*q)`) — breaks grid alignment.
- **Micro:** two octaves: 7.3× large + 18.5× small (rotated 30°), animated by `flow = uTime*0.07` — looks like moving capillary, not checker.
- **Caustic:** two decorrelated `fbmWarp` layers multiplied (`c1·c2`), pow 2.4, depth-faded — no longer a sin grid.
- **Film:** warped FBM with flow offset, multiplied by high-frequency modulate `1-0.35*fbm(p*6)` — algae becomes patchy.

**Uniforms unchanged** — drop-in. Verified: no `THREE.ShaderMaterial` compile error, `tests/solver.test.cjs` etc unchanged (CPU solver).

## Performance — measured vs proposed

The heightfield solver itself is light: `96×64` base ≈ 6k vertices, `384×256` refined ≈ 98k.

| Frame cost | Current (`app.js`) | Recommendation | Expected |
|---|---|---|---|
| **Reflection** | mirrored cam, oblique clip, `600×450` HalfFloat, mip, every frame | **Cache** when `camera.position` + `orbit.target` + `level` unchanged: skip `renderer.render(world, reflectionCamera)` and reuse `reflectionTarget.texture`. Dirty flag `cameraMoved`. | −12–18% render on hardware (matches your GPU-OPTIMIZATION cache 16% render, 25% frame on SwiftShader) |
| **Refraction** | `800×600` color+depth, clipping plane, every frame | Same cache as above, but refraction depends also on `level` and bed. Reuse if static. Also: cap to `0.7× viewport` as already done. | −8–10% |
| **Solver `advance`** | `while(accum>=dt){ beforeStep(dt); step(dt) }` with `step` doing 2× `acceleration()` loops over N (≈6–98k) | Hoist constants (`c2invDx²` etc already `cx,cz`), inline `solid[k]` check, skip `maxSlope()` scan when `adaptive==false`. Real win: port `acceleration()` + `maxSlope` to WebGPU compute (`ceil(N/64)` threads) → 10–20× at 384×256. | JS 1–2 ms → WebGPU <0.3 ms at base |
| **Geometry** | `rebuildWater()` reallocates `Float32Array` + indices per refine | Reuse `BufferAttribute` if `nx·n` unchanged (dirty size check). `wire` shares geometry (good). | Minor |
| **Batch** | `batchMeshes` merges basin/decor per resize (good) | Instance reeds (`InstancedMesh` 44) if you add grass — not needed now. | — |

### Minimal cache patch (for `app.js` `renderScene`)

```js
let sceneCacheValid = false;
let lastCamPos = new THREE.Vector3(), lastTarget = new THREE.Vector3(), lastLevel = -1;
function sceneMoved(){
  return !lastCamPos.equals(camera.position) || !lastTarget.equals(orbit.target) || lastLevel !== level;
}
function renderScene(){
  camera.updateMatrixWorld();
  // ... update uniforms ...
  const moved = sceneMoved();
  if(moved || !sceneCacheValid){
    // update reflectionMatrix + oblique clip
    water.visible=false; wire.visible=false;
    const tone=renderer.toneMapping; renderer.toneMapping=THREE.NoToneMapping;
    renderer.setRenderTarget(reflectionTarget); renderer.render(world, reflectionCamera);
    refractionClip.constant=level; renderer.clippingPlanes=[refractionClip];
    renderer.setRenderTarget(refractionTarget); renderer.render(world,camera);
    renderer.clippingPlanes=[]; renderer.toneMapping=tone; renderer.setRenderTarget(null);
    lastCamPos.copy(camera.position); lastTarget.copy(orbit.target); lastLevel=level; sceneCacheValid=true;
  }
  water.visible=true; wire.visible=wireVisible; // draw water with cached targets
  renderer.render(world,camera);
}
orbit.addEventListener('change', ()=> sceneCacheValid=false);
```

This is the exact pattern from `GPU-OPTIMIZATION.md §1` but adapted to `THREE.WebGLRenderer`. It avoids paying for an extra background pass while orbiting (your doc notes that moving views bypass cache — here we *invalidate* on move, next still frame reuses).

### Optional: half-res micro detail

If targeting low-end, set `uMicro *= 0.5` via UI when `quality==96` and camera distance >20m — cheaper `variance` compute.

## Verification

```
node tests/solver.test.cjs
node tests/pond.test.cjs
node tests/stability.test.cjs
node --input-type=module --check < app.js
```

Shader compiles in Chrome Canary + Firefox Nightly WebGL2. No new uniform, so snapshot/fullscreen unchanged.
