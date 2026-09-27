# Frontier · Tarantula (Brachypelma hamorii)

Real-time 3D Mexican red-knee tarantula in a procedurally carved limestone cave. Built with three.js r180 and Vite.

```bash
cd tarantula
npm install
npm run dev        # http://localhost:5173  (game)  ·  /dev.html (studio viewer)
npm run build      # production build -> dist/
```

## Controls
| Input | Action |
|---|---|
| W A S D / arrows, Shift | Move (camera-relative) / run |
| Mouse drag, wheel | Orbit, zoom |
| Q (hold) | Threat display: rears up, forelegs and palps raised, fangs unsheathed |
| E / Space | Strike: anticipation, lunge, forelegs pin, downward fang stab with venom pumps |
| R | Urticating-hair flick: leg IV scrapes the abdomen and kicks setae into the air |
| Tab | Toggle autonomous AI |
| G / P | Slow motion (0.2×) / pause |
| T, C, H | Torch, cinematic camera, hide UI |

The UI panel also switches quality (Low/Med/High/Ultra). This changes fur shell count, render resolution, shadow resolution and bloom.

## What's in it
- **Anatomy** (`src/spider/anatomy.js`): proportions of an adult female (body ≈ 5.3 cm, leg IV ≈ 6.7 cm, leg formula 4-1-2-3). 1 unit = 1 cm. Colours are sampled from reference photos: orange patellae with pale distal halves, cream tibia/metatarsus rings, a carapace with a tan fringe, and an abdomen with reddish-brown setae.
- **Setae**: the dense pile is shell-rendered fur with a per-strand coverage fallback so it doesn't alias. Long guard hairs are instanced curved strands; the pale ones sit on the legs and the reddish ones on the abdomen.
- **Locomotion** (`Tarantula.js`, `Limb.js`): alternating tetrapod gait (L1-R2-L3-R4 / R1-L2-R3-L4), about 10% phase lag inside each group. Duty factor drops from 0.75 to 0.56 as speed rises, and speed comes mostly from stride frequency, as measured in real tarantulas. The leg IK keeps the patella as the leg's highest point, the metatarsi steeply angled and the tarsi flat on the ground.
- **Wall/ceiling walking**: the body frame follows the surface normal, which is sampled by raycasts against a BVH (`SurfaceWorld.js`). It moves across concave and convex edges and works upside down.
- **Cave** (`src/world`): signed-distance-field cave meshed with surface nets in a Web Worker. It has baked AO and cavity, plus a procedural limestone shader with strata, iron staining, calcite flowstone, wet drip streaks and floor sediment. There's a sunlit shaft with a volumetric beam and dust, climbable boulders, gravel and roots.
- **Rendering**: PCF soft shadows from the sun shaft and the torch, image-based lighting captured from the cave itself, MSAA HDR composer, bloom, Khronos Neutral tone mapping, vignette and film grain.

`window.__game` exposes the scene for debugging. `?manual` switches to on-demand rendering for automated captures, `?nohud` hides the UI, `?q=ultra` sets the starting quality and `?mode=ai` starts in AI mode.

Quality metrics, the automated audit tools (`tools/`) and a roadmap for further animation polish are documented in [IMPROVING.md](IMPROVING.md).
