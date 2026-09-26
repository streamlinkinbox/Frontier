# Approved web controls → C++ property audit

Target: `SultanAladin/Frontier-`, revision `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`.
Web reference: `41cf997` (the complete approved UI checkpoint). This is a source audit, not a claim that the redesigned inspector has been implemented.

## Existing connection

`Projects/Project-Zero/Source/GameExecution.cpp:1642–1681` selects either `CelestialSequence::BuildSheet` or `EditorFeedSequence::BuildSheet`. Celestial edits are read back by `CelestialSequence::ApplySheet`, then the sheet is rebuilt. Engine panels borrow the project-owned property sheet; do not introduce project includes into Engine/Editor.

`Engine/Editor/EditorInstance.h` supports Slider, Switch, AxisVec3, Colour, Select and Readout. There are currently six groups per sheet, ten properties per group, six select options, 20-character option strings and 28-character labels. The web's richer diagrams, file input, custom-outline editing and some long lists do not fit that protocol unchanged. Do not truncate a list or a diagram into a plausible-looking control.

## Audit

| Approved menu | Existing C++ counterpart | Conversion or missing support |
|---|---|---|
| Sun disk diameter | `CelestialSequence::SunDiskSize`; Sun / Angular Diameter | Degrees already match; approved card drawing still needed. |
| Sun time / day cycle | `Observation.LocalHours`, `Clock.Animate`, `Clock.SpeedTimes`; Sun / Time | Hours map directly. C++ also has latitude, longitude, month and day; preserve astronomical behaviour. |
| Sun azimuth/elevation drag | `Solved.Sun.Azimuth`, `Solved.Sun.Elevation` | **Read-only solved quantities** in C++. A genuine manual-direction mode is required; assigning the readouts would be fake. |
| Sun illuminance and temperature | `Light.Intensity`, `SunDirect`, `Light.Colour` | Existing intensity is gain, not the web's illuminance units. No colour-temperature control in this sheet. Establish photometric conversion and temperature-to-colour policy first. |
| Sunlight / disk / day-cycle switches | `Shown[Sun]`, animation control | Do not equate whole-Sun visibility with independent sunlight/disk switches. Separate engine support must be checked/added. |
| Atmosphere scattering / haze / ozone | `Medium.RayleighStrength`, `MieStrength`, `MieAnisotropy`, `OzoneStrength` | Rayleigh multiplier maps. Web haze/ozone percentages are not automatically the C++ multipliers; define ranges and round-trip conversions. |
| Atmospheric density and ground reflectance | `Medium.RayleighScaleHeight`, `MieScaleHeight`; `GroundAlbedo[3]` on Sky | km→m; choose which scale height each card edits. Web scalar reflectance is not the same as RGB albedo. |
| Ground fog | `Fog.HeightEnabled`, `HeightDensity`, `FalloffHeight`, `SunScatter`, `HeightColour` | Web fog percentage has no documented mapping to density in inverse meters. Keep physical units or define the mapping explicitly. |
| Clouds coverage / base / thickness | `Cloud.Coverage`, `Cloud.Base`, `Cloud.Thickness`; CloudLayer / Shape + Altitude | Coverage %÷100, km×1000. Respect `CeilingMetres`; C++ also has cloud types, density, scale and wind following. Smooth sky illustration is UI-only, not a rendered sky. |
| Local Clouds / Local Fog shared menus | `LocalCloud`, `LocalFog` (`LocalVolumeSettings`) | Independent coverage/density/scale exist, but local volumes do **not** have the same altitude/thickness fields as the global cloud layer. Bind shared visual menus deliberately; never edit `Cloud` from a local selection. |
| Combined volume bounds & position | `LocalVolumeSettings::Centre[3]`, `HalfSize[3]` | Dimensions must become half-extents. Engine is **Z-up**, while web cards label Y as vertical: settle axis/sign conversion before wiring. Box bounds exist. |
| Sphere/cylinder/cone/custom-outline volume shapes | LocalVolumeSettings is explicitly box-bounded | **Not supported by the existing volume definition.** New shape representation and matching CPU/shader intersection rules are needed; a selector alone is insufficient. |
| Wind | `Wind.Speed`, `Bearing`, `Gust`, `Turbulence`, `Shear`, `Veer`, `Steadiness` | Speed m/s and bearing degrees match; gust %÷100. The draggable direction/magnitude diagram is new UI work. |
| Rain / Snow / Hail | `Precip.Category`, `RateMillimetresPerHour`, `SizeScale`, `Density`, `FollowWind`, `WindDrift` | Map enumeration names, not numeric indices (C++ also has Drizzle/Sleet). Rate maps; millimeter diameter is not `SizeScale`. Independent web fall speed lacks a matching editable field here. |
| Moon direction / size / phase | `MoonSlots[S].Azimuth`, `Elevation`, `Size`, `Bright`, `Glow`, `Phase`, `FollowSky`, `Preset` | Slots are supported. Existing size slider limits conflict with unlimited web size; roll/pitch controls are not in the slot sheet. Phase conventions require explicit conversion. |
| Stars | `StarBrightness`, `StarSize`; Catalogue readouts | Brightness/point size supported. Twinkle controls and a six-target image baking workflow are not in this sheet. |
| Rainbow | `Rainbow.Enabled`, `Intensity`, `Width`, `SecondaryGain`, `AlexanderBand` | Several scalar controls map directly; new illustration and per-effect baked-image source remain work. |
| Lens flare | `Flare.Enabled`, `Category`, `Intensity`, `GhostCount`, `HaloRadius`, `Chromatic`, `ApertureBlades` | C++ selects one effect category. The approved mixable effects and separate ghost shapes require additional representation; do not collapse them into one exclusive select. |
| Baking at the bottom | Sky has `Fetch Baked Dome`, `QuerySkyDomeLive`, `BakeSkyDome`, save/load of its staged dome | Actual sky dome path exists (256² RGBA16F), but not the web's six independent PNG/JPEG/WebP sources. Keep stale-dome guards; do not label a pending request as rendered. |
| Folders / collections | Folder category, depth-based roster, visibility, selection/reparenting | Folders exist. Reference collections distinct from parent ownership are not a first-class category in `EditorInstanceCategory`; membership semantics need extension. |
| Camera | Fly-camera FOV; cinematic optics sheet has focal length, aperture, focus, sensor width and ISO | Fly-camera FOV is exposed. Cinematic sheet initializes several defaults rather than establishing a complete write-back path; verify per-control edits before claiming support. Sensor/view-angle diagram remains presentation work. |
| Mesh / transform | Geometry roster and placement sheet, gizmo editing, material index | Preserve the demonstrated gizmo route. Some placement-sheet axes are explicitly read-only. Primitive dimensions/generation are not implemented by restyling this sheet. |
| Materials | Existing MaterialInspector and material records; placement albedo/emission/roughness | Reuse native material ownership; audit each of the web's 20+ channels. The inline preview must use a genuine CPU counterpart, not a static screenshot. |
| Terrain / Forest / species / water bodies | No equivalent full approved card sets found in the audited editor/property feeds | Dedicated authoring representation and property feeds still required. This is not a claim that the engine has no terrain/water/foliage code elsewhere. |

## UI-only work that must not change simulation

Text-only selected-entity header; neutral charcoal style; card layout; compact green/red switches without adding Shadows/GI tiles; bottom baking layout; diagrams; appropriate outliner icons. Shared widgets must use stable identifiers, explicit enablement and real input hit areas. Maintain the existing selection, folder visibility and CPU proof behaviour.

## Code locations to revisit during the inspector phase

- `Projects/Project-Zero/Source/CelestialSequence.cpp:945–1435`: property construction and read-back.
- `Projects/Project-Zero/Source/EditorFeedSequence.cpp:422–650`: folder, placement and camera sheets.
- `Engine/DisplayPresentation/VolumetricMedia.h:84`: box-bounded local volumes, Z-up.
- `Engine/Editor/EditorInstance.h:145–201`: current property protocol limits.
- `Engine/Editor/InspectorPanel.cpp`: current generic card drawing.

No C++ outliner or inspector was edited in the baseline/IconArt phase.
