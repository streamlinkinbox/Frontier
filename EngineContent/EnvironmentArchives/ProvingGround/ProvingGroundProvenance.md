# Proving Ground Provenance

`EngineContent/EnvironmentArchives/ProvingGround/` is the VehicleHMI demo's proving ground, as the demo itself built it.
The demo is `SultanAladin/Frontier`, branch `arena/e9bd7db5-frontier`, commit `6afb6f00b5f25c45f7cda92c7da65b64845768f3`,
folder `Experimental/VehicleHmiPrototype/`. It contains the training track, the boundary barriers, the tarmac markings
and the cones. It also has the hangar, the tunnel, the control tower, the tyre walls, the street lamps, the lamp posts
and the breakable and stress-test pools.

The files were captured with `Exhibits/RestirGiParity/Code/CaptureReference.sh` (settings `quality=low&omit=Cabin`). They
were then written by `Exhibits/SceneViewport/Code/ExportProvingGround.py`:

| File | Bytes | Content |
|---|---:|---|
| `ProvingGround.facetspace` | 3,769,100 | FSP1 scene topology: 58 facet sets and 2,094 occupants, every scalar float64, unchanged from the demo |
| `Materials.bin` | 3,936 | The demo's `Materials` creation upload, with its bring-up rewrites applied in order |
| `VehicleProfile.bin` | 10,448 | `Vehicle record` of refresh 0 |
| `EffectProfile.bin` | 1,120 | `Effect record` of refresh 0 (idle derez and spark settings) |
| `CloudProfile.bin` | 544 | `Cloud renderer v2` of refresh 0 (the demo's own clouds switched off) |
| `CloudShadowProfile.bin` | 48 | `Projected cloud shadows` of refresh 0 (switched off) |

- **No car yet.** The vehicle body and its four wheels (occupants 0–4) stay in the file.
  `IlluminationAssets::OmitVehicle` marks them inactive before bring-up, so no step draws, unions, traces or lights them.
  Bring them back by leaving the flag off.
- **Luminaires.** The 20 street lamps and 10 lamp-post lanterns are not stored in a file. `IlluminationHost` places them
  from the occupants `Light pole head N` and `Point lamp lantern N`, the same placements `LevelStructure.js` produced, and
  `LuminaireRegister` packs them every refresh.
- **Byte checks.** `RestirGiParityValidation … live` checks the packed register byte for byte against the demo's
  `Clustered light records`.
