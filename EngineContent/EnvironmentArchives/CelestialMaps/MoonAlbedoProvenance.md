# Six-Body Lunar & Planetary Satellite Atlas Provenance

`EngineContent/EnvironmentArchives/CelestialMaps/` contains the six real `2048 × 1024` equirectangular sRGB moon maps
inherited from `SultanAladin/Frontier` (`arena/e9bd7db5-frontier`) and `SultanAladin/Frontier-`
(`EngineContent/CelestialTextures/`):

| Atlas Layer | Roster Body | Source File | Upstream Path (`SultanAladin/Frontier-`) | Dimensions |
|---|---|---|---|---|
| `0` | `Luna` | `MoonAlbedoSource2048.jpg` | `EngineContent/CelestialTextures/luna_2k.jpg` | `2048 × 1024` sRGB |
| `1` | `Ember` | `Ember2048.jpg` | `EngineContent/CelestialTextures/ember_2k.jpg` | `2048 × 1024` sRGB |
| `2` | `Glacier` | `Glacier2048.jpg` | `EngineContent/CelestialTextures/glacier_2k.jpg` | `2048 × 1024` sRGB |
| `3` | `Sulfur` | `Sulfur2048.jpg` | `EngineContent/CelestialTextures/sulfur_2k.jpg` | `2048 × 1024` sRGB |
| `4` | `Shroud` | `Shroud2048.jpg` | `EngineContent/CelestialTextures/shroud_2k.jpg` | `2048 × 1024` sRGB |
| `5` | `Shard` | `Shard2048.jpg` | `EngineContent/CelestialTextures/shard_2k.jpg` | `2048 × 1024` sRGB |

- `MoonAlbedoSource2048.jpg` (`96996f2b89e1f0164b051bd2c1bd3f53516c169b`) is byte-identical to `luna_2k.jpg`
  (`Experimental/VehicleHmiPrototype/Assets/Celestial/luna_2k.jpg`).
- `Ember2048.jpg`, `Glacier2048.jpg`, `Sulfur2048.jpg`, `Shroud2048.jpg`, and `Shard2048.jpg` are byte-identical to
  `SultanAladin/Frontier-` commit `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b` (`EngineContent/CelestialTextures/*_2k.jpg`).
- `PlanetaryProjection::LoadLunarAlbedoAtlas` decodes all six `2048 × 1024` JPEGs directly into linear-radiance
  `RGBA16F` slices (applying exact IEC 61966-2-1 sRGB EOTF prior to GPU filtering); `Automation/PrepareMoonAlbedo.py`
  remains available to emit uncompressed `MoonAlbedo2048.ppm` (git-ignored) for legacy PPM tests (`LunarAlbedoMap::LoadPpm`).
- Orientation follows the upstream atlas convention: prime meridian at `U = 0.5`, north pole at `V = 0.0`.
- Per `SultanAladin/Frontier-`'s `NOTICE.md`, runtime celestial texture provenance is inherited from the upstream
  consolidation without inventing new third-party ownership or commercial redistribution claims.
