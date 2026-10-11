# HTML-reference drawing corrections

Browser reference paths in this document are relative to `Experimental/FrontierEditor/`.

The existing HTML inspector is the visual authority for this correction. No engine settings or render support were added or removed.

## Precipitation type icons

`src.jsx` uses Lucide `CloudRain`, `CloudSnow` and `CloudHail`, not standalone droplets, snowflakes or hailstones. The native type selector now draws those same 24×24 paths, with the reference's round 2-unit strokes and selected/unselected icon colours. The paths come from the workspace's locked `lucide-react` 0.468.0 dependency; their ISC notice is retained in `Engine/DisplayPresentation/InspectorReferenceDraw.NOTICE`.

Only type-selector artwork changed. Particle-size illustrations and physics controls remain unchanged. The native backend's additional Drizzle and Sleet entries reuse CloudRain and CloudHail respectively; these two types have no separate HTML-reference buttons.

## Camera aperture

`property-graphics.jsx::Iris` is transcribed with its 220×220 coordinates: 97/88-unit rings, radial background, 48 rim ticks, eight alternating concave blades with circular outside edges, octagonal opening, centre glow and dot. The native layout uses the HTML's 132-pixel iris size. The original opening formula is retained, with saturation beyond the HTML's f/16 limit for the native backend's extended range to f/22. The HTML's interpolation animation is not reproduced; native geometry follows the current control value directly.

The sensor/FOV card remains a sensor diagram; the iris is only in the aperture card. Numeric pupil and in-focus depth readouts accompany it.

## Subject plane / focus

`camera-graphics.jsx::DepthOfField` is transcribed in its 360×174 coordinates, fitted with the same centred aspect-preserving behaviour. It includes the dark rounded field, tinted sharp range, dashed near/far limits, 17 focus targets, logarithmic metre ticks, plane line and triangular indicator, and circular crosshair drag handle. Pointer coordinates now use that same fitted viewBox and the HTML's rounded logarithmic distance mapping.

The CSS `blur(1.7px)` on out-of-focus targets is approximated by a native Gaussian sample kernel; native antialiasing and font rasterisation are not claimed to be browser-pixel-identical. No scene-camera blur is implied: aperture/focus remain optical diagnostics, as documented in the Camera delivery.

## Verification

Both modified native panels compiled with `-Wall -Wextra -Werror`. Existing matching-mode builds were reused after recompiling the two changed native translation units and copying the new drawing header. The engine-binding code was unchanged.

- Camera: **43 checks passed** in Release, Debug and ASan/UBSan, including HTML iris-radius references and logarithmic focus round trips.
- Weather: **1684 checks passed** in each mode, including native type selection and existing simulation/optics tests.
- All six Camera/Weather source/capture manifests matched after regeneration.
- Native captures were visually inspected. Camera and Weather galleries serve the refreshed files at ports 5186 and 5185.
- Camera recorder frames: 768 / 1088 / 6432 bytes (Release / Debug / Sanitized), below the existing 8 KiB per-function gate. These remain Linux/GCC measurements, not Windows certification.

Rendering limitations, project/editor ownership and Water Bodies / Fluids scope are unchanged.
