# Frontier

## Update: camera modes & per-effect settings
- **Default tier is GTX** (it was faster and looked better on RDNA4). RTX is opt-in via `?tier=rtx` or the HUD, and is now lighter (160×80×160 @ 0.2 m).
- **Camera**: press `C` or use the HUD buttons to switch between Chase / Free / Action (faces the latest explosion, then the plume, then the tornado) / Tornado.
- **Slider groups**: Tyre smoke, Explosion/fire (fire glow, soot opacity, soot darkness, soot fade), Tornado (dust), Lighting & motion. Settings are saved under `smokeSettings2`.

## Update: puddles (GTX) · RTX tier removed
- **3 puddles**: clear (front-right of start), mud (front-left), clear (past the barriers).
- Each puddle: 128×128 GPU wave-equation surface. Tyres depress the water and push bow waves; the shore reflects waves. Mud is slower, more damped and opaque; clear water is transparent with Fresnel sky reflection, sun glints and foam.
- **Splashes**: 32k GPU droplets (side sheets + rooster tail), colliding with the spinning tyres and the car body box.
- The car loses grip and gets water drag in puddles (more in mud). Wet tyres throw no sand or smoke.
- Sliders under 💧 Puddles: splash amount, wave strength, water drag. Test flag: `?puddletest`.
- RTX tier removed; GTX (default) and Low remain.

## Update: real puddles in dented terrain
- The ground is dented into bowls (up to 26 cm deep) with irregular shorelines. The car drops into them, pitching and rolling with the terrain.
- The water is a mass-conserving shallow-water fluid ("virtual pipes") on a 128×128 grid per puddle, with 4 substeps per frame. It has real volume, flows downhill and settles still. Tyres push it aside, making bow waves, a trough and a wake that sloshes back.
- Rendering: Beer–Lambert absorption over the real water depth (thin edges show the wet bed, deeper water is tinted), Fresnel sky reflection, sun glints, and foam on disturbed water. Mud is opaque and viscous.
