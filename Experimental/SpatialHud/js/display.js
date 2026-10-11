//===============================================================================================//
// 📦 The display: the interface rendered to its own texture, and glass that samples it.
//
//    Everything before this drew the interface straight to the swapchain and then laid a glass
//    pass over the top with additive blending. That is a decal. Additive light can only ADD, so
//    the "glass" could put a reflection on the picture but could never bend, displace, dim or
//    tint what was underneath it — and bending what is underneath is the entire physical content
//    of the word glass. Every screenshot of it was a flat image with a highlight painted on.
//
//    So the panel now renders into a texture of its own and the tablet samples that texture,
//    which is how a device screen is done in a real engine. What it buys, in order of how much
//    you can see it:
//
//      · REFRACTION. The display sits 2.4 mm behind the outer face of the cover glass. Looking in
//        at an angle, the image is displaced by that gap — the amount a real screen's content
//        slides under its own glass. Impossible to fake additively: it is a resample.
//      · BLOOM. Emissive interface elements bleed into the glass and into the bezel. This is the
//        thing a target exists for and the reason the earlier notes kept saying "not yet".
//      · ABSORPTION + the black level. Transmission is (1 - Fresnel), so when the room wins at a
//        glancing angle the interface correctly FADES instead of staying fully bright with a
//        highlight added on top of it.
//      · The display is a grid of pixels at its own fixed resolution, independent of the window.
//
// 🔴 THE PROJECTION IS OFF-AXIS, AND IT HAS TO BE.
//
//    The obvious way to fill a screen texture is to render the interface orthographically, face
//    on, and paste it on the quad. That is wrong here and it throws away the thing this whole
//    experiment is about: the fibre volume is a hundred-odd millimetres DEEP, and an orthographic
//    bake flattens it to a sticker. The parallax would die the moment the camera moved.
//
//    The fix is the projection a portal or a mirror uses: keep the apex at the real eye, and make
//    the near plane the panel rectangle itself rather than a symmetric window about the view
//    axis. Then texture UV and panel coordinate are the SAME quantity by construction, the depth
//    behind the glass projects exactly as it did when it was drawn straight to the screen, and
//    sampling the texture at the point where the sightline crosses the glass reproduces the old
//    image to the pixel. Which means any offset from that point is real refraction measured
//    against a correct baseline, instead of a smear that happens to look busy.
//
//    DisplayClip() below is that matrix. The derivation is in the comment on it.
//===============================================================================================//

// The display's own resolution, fixed and independent of the window — a panel has the pixels it
// has. 1440 x 845 is 1.7041:1 against the panel's 0.460 x 0.270, which is 1.7037:1.
export const DisplayWidth = 1440;
export const DisplayHeight = 845;

// Bloom runs at a quarter in each direction. Glow is low-frequency by definition and paying full
// resolution for it is the classic way to spend half a frame on something nobody can see.
export const BloomWidth = 360;
export const BloomHeight = 212;

export const Glass = {
    thickness: 0.0024,       // [m] outer face of the cover glass to the emitting plane
    index: 1.5170,           // crown glass

    bloomThreshold: 0.42,    // the interface is mostly dark; only the lit parts should glow
    bloomStrength: 0.55,
};

//---------------------------------------------------------------------------------------------//
// 🔴 The off-axis projection.
//
//    Work in panel space, where the glass is the plane z = 0, the interface and the fibre volume
//    live at z <= 0, and the eye is at E with E.z > 0. For a point P, the sightline E -> P crosses
//    the glass at
//
//        t     = E.z / (E.z - P.z)
//        cross = E.xy + (P.xy - E.xy) * t
//
//    and we want that crossing, divided by the panel half extents, to BE the normalised device
//    coordinate. So take the standard off-axis frustum about an eye at the origin looking down
//    -z, with the near plane at E.z and the window set to the panel rectangle as the eye sees it:
//
//        left = -hw - E.x    right = hw - E.x    bottom = -hh - E.y    top = hh - E.y
//
//    Then for Q = P - E, ndc.x = (2n*Q.x + (r+l)*Q.z) / ((r-l) * -Q.z), and substituting gives
//    (E.z*P.x - E.x*P.z) / (hw * (E.z - P.z)), which is exactly cross.x / hw. The window is the
//    panel, so the panel fills the target exactly, and depth still projects.
//
//    Composed right to left: panel-from-world, then the translation to the eye, then the frustum.
//---------------------------------------------------------------------------------------------//

// [m] How far in FRONT of the glass the display frustum's near plane sits. The interface stacks
// toward the viewer one millimetre per layer and reaches 4.4 mm at its tallest, so this is that
// with room to spare. It costs nothing but depth range.
export const Approach = 0.020;

export function DisplayClip(rows, eye, halfWidth, halfHeight) {
    // Panel from world. The rotation is orthonormal, so its inverse is its transpose, and the
    // translation comes along as -(transpose * origin).
    const rx = [rows[0][0], rows[0][1], rows[0][2]];
    const ry = [rows[1][0], rows[1][1], rows[1][2]];
    const rz = [rows[2][0], rows[2][1], rows[2][2]];
    const origin = [rows[0][3], rows[1][3], rows[2][3]];

    const column = (A) => [rx[A] * 1, ry[A] * 1, rz[A] * 1];
    const axisU = column(0), axisV = column(1), axisW = column(2);

    const toPanel = (world) => [
        axisU[0] * (world[0] - origin[0]) + axisU[1] * (world[1] - origin[1]) + axisU[2] * (world[2] - origin[2]),
        axisV[0] * (world[0] - origin[0]) + axisV[1] * (world[1] - origin[1]) + axisV[2] * (world[2] - origin[2]),
        axisW[0] * (world[0] - origin[0]) + axisW[1] * (world[1] - origin[1]) + axisW[2] * (world[2] - origin[2]),
    ];
    const eyePanel = toPanel(eye);

    // A panel seen exactly edge on has no window at all. Hold the eye a millimetre in front so the
    // matrix stays finite; the tablet is a sliver of nothing at that angle anyway.
    const depth = Math.max(eyePanel[2], 0.001);

    // 🔴 THE NEAR PLANE CANNOT BE THE PANEL, EVEN THOUGH THE WINDOW IS.
    //
    //    The frustum's window is the panel rectangle — that is the whole point of an off-axis
    //    projection, and it is what makes a texture coordinate equal a panel coordinate. But
    //    putting the near PLANE there too clips away everything in front of it, and the entire
    //    interface is in front of it: the layout stacks its figures toward the viewer a millimetre
    //    at a time, up to 4.4 mm. Every one of them was being discarded by the GPU. The only
    //    things left were the fibres, which live BEHIND the glass at negative z — which is exactly
    //    what the device showed: a black screen with the wallpaper still moving on it.
    //
    //    So the plane moves toward the eye by Approach, and the window shrinks by the same ratio.
    //    Because the window and the plane scale together, 2n/(r-l) and (r+l)/(r-l) are both
    //    unchanged: every sightline still crosses the glass exactly where it did, the corners
    //    still land on +/-1, and only the depth range moves. Nothing about the parallax changes.
    const near = Math.max(depth - Approach, depth * 0.05);
    const reach = near / depth;
    const far = depth + 8;

    const left = (-halfWidth - eyePanel[0]) * reach;
    const right = (halfWidth - eyePanel[0]) * reach;
    const bottom = (-halfHeight - eyePanel[1]) * reach;
    const top = (halfHeight - eyePanel[1]) * reach;

    // Column-major, WebGPU clip depth 0..1, looking down -z.
    const frustum = [
        (2 * near) / (right - left), 0, 0, 0,
        0, (2 * near) / (top - bottom), 0, 0,
        (right + left) / (right - left), (top + bottom) / (top - bottom), far / (near - far), -1,
        0, 0, (far * near) / (near - far), 0,
    ];

    // Panel-from-world as a column-major 4x4, with the eye translation folded in.
    const view = [
        axisU[0], axisV[0], axisW[0], 0,
        axisU[1], axisV[1], axisW[1], 0,
        axisU[2], axisV[2], axisW[2], 0,
        -(axisU[0] * origin[0] + axisU[1] * origin[1] + axisU[2] * origin[2]) - eyePanel[0],
        -(axisV[0] * origin[0] + axisV[1] * origin[1] + axisV[2] * origin[2]) - eyePanel[1],
        -(axisW[0] * origin[0] + axisW[1] * origin[1] + axisW[2] * origin[2]) - eyePanel[2],
        1,
    ];

    const out = new Float32Array(16);
    for (let c = 0; c < 4; c++) {
        for (let r = 0; r < 4; r++) {
            out[c * 4 + r] = frustum[0 * 4 + r] * view[c * 4 + 0] + frustum[1 * 4 + r] * view[c * 4 + 1]
                           + frustum[2 * 4 + r] * view[c * 4 + 2] + frustum[3 * 4 + r] * view[c * 4 + 3];
        }
    }
    return { clip: out, eyePanel, near };
}

// Line width on the display is measured in the display's own pixels, so the fibres need the scale
// of THAT projection and not the window's. The off-axis frustum has no single vertical half angle,
// but its window spans 2*hh at a distance of E.z, which is the quantity the strip raster wants.
export function DisplayProjectionScale(eyePanelZ, halfHeight) {
    return 0.5 * DisplayHeight * Math.max(eyePanelZ, 0.001) / halfHeight;
}

export const DISPLAY_WGSL = String.raw`
//-----------------------------------------------------------------------------------------------
// The display target and its glow, and the glass that reads them.
//-----------------------------------------------------------------------------------------------

@group(0) @binding(4) var DisplaySheet : texture_2d<f32>;
@group(0) @binding(5) var DisplaySampler : sampler;
@group(0) @binding(6) var BloomSheet : texture_2d<f32>;

struct CoverVarying {
  @builtin(position) Position : vec4f,
  @location(0) Texture : vec2f,
};

@vertex
fn vsCover(@builtin(vertex_index) Vertex : u32) -> CoverVarying {
  // One oversized triangle. Two triangles meet along a diagonal and a seam can show there.
  var corner = array<vec2f, 3>(vec2f(-1.0, -3.0), vec2f(-1.0, 1.0), vec2f(3.0, 1.0));
  var out : CoverVarying;
  out.Position = vec4f(corner[Vertex], 0.0, 1.0);
  out.Texture = vec2f(corner[Vertex].x * 0.5 + 0.5, 0.5 - corner[Vertex].y * 0.5);
  return out;
}

// Bright pass and downsample in one. A four-tap box at the quarter grid is a correct average of
// the sixteen source pixels it covers, because bilinear is already averaging each group of four.
@fragment
fn fsBloomCut(in : CoverVarying) -> @location(0) vec4f {
  let step = 1.0 / vec2f(${DisplayWidth}.0, ${DisplayHeight}.0);
  var total = vec3f(0.0);
  for (var y = -1; y <= 1; y = y + 2) {
    for (var x = -1; x <= 1; x = x + 2) {
      let at = in.Texture + vec2f(f32(x), f32(y)) * step;
      let sample = textureSample(DisplaySheet, DisplaySampler, at).rgb;

      // Soft knee. A hard threshold makes glow pop on and off as a value crosses it, which on a
      // needle sweeping past a tick is extremely visible.
      let level = max(max(sample.r, sample.g), sample.b);
      let over = max(level - ${Glass.bloomThreshold}, 0.0);
      total += sample * (over / max(level, 1.0e-4));
    }
  }
  return vec4f(total * 0.25, 1.0);
}

// Separable Gaussian, nine taps, weights from the binomial row. Direction comes in as a step so
// one shader serves both axes.
fn BloomBlur(at : vec2f, step : vec2f) -> vec3f {
  var total = textureSample(BloomSheet, DisplaySampler, at).rgb * 0.2270270;
  let weight = array<f32, 4>(0.1945946, 0.1216216, 0.0540541, 0.0162162);
  for (var i = 0; i < 4; i = i + 1) {
    let offset = step * f32(i + 1);
    total += textureSample(BloomSheet, DisplaySampler, at + offset).rgb * weight[i];
    total += textureSample(BloomSheet, DisplaySampler, at - offset).rgb * weight[i];
  }
  return total;
}

@fragment
fn fsBloomAcross(in : CoverVarying) -> @location(0) vec4f {
  return vec4f(BloomBlur(in.Texture, vec2f(1.0 / ${BloomWidth}.0, 0.0)), 1.0);
}

@fragment
fn fsBloomDown(in : CoverVarying) -> @location(0) vec4f {
  return vec4f(BloomBlur(in.Texture, vec2f(0.0, 1.0 / ${BloomHeight}.0)), 1.0);
}

//-----------------------------------------------------------------------------------------------
// The glass. It is OPAQUE: it owns every pixel of the panel face, because it is now the thing
// that decides what the interface looks like rather than something laid over the top of it.
//-----------------------------------------------------------------------------------------------

struct GlassVarying {
  @builtin(position) Position : vec4f,
  @location(0) Local : vec2f,
  @location(1) World : vec3f,
};

@vertex
fn vsGlass(@builtin(vertex_index) Vertex : u32) -> GlassVarying {
  let sign = vec2f(select(-1.0, 1.0, (Vertex & 1u) != 0u),
                   select(-1.0, 1.0, (Vertex & 2u) != 0u));
  let corner = sign * R.face.xy;                       // the PANEL rectangle, not the body's
  let world = vec3f(dot(R.rowX.xyz, vec3f(corner, 0.0)) + R.rowX.w,
                    dot(R.rowY.xyz, vec3f(corner, 0.0)) + R.rowY.w,
                    dot(R.rowZ.xyz, vec3f(corner, 0.0)) + R.rowZ.w);

  var out : GlassVarying;
  out.Position = G.viewClip * vec4f(world, 1.0);
  out.Local = corner;
  out.World = world;
  return out;
}

// Panel coordinate to display texture coordinate. The projection was built so these are the same
// quantity up to the half extents and the usual v flip.
fn DisplayTexture(local : vec2f) -> vec2f {
  return vec2f(local.x / R.face.x * 0.5 + 0.5, 0.5 - local.y / R.face.y * 0.5);
}

// 🔴 Refraction, and why there is a per-channel offset.
//
//    The emitting plane is Glass.thickness behind the outer face. A sightline entering the glass
//    bends toward the normal, so by the time it reaches the emitting plane it has walked sideways
//    by a different amount than it would have done in air. The display texture holds what is seen
//    along the UNBENT line, so the correction is the difference between where the two rays land:
//
//        offset = thickness * (bent.xy / |bent.z| - straight.xy / |straight.z|)
//
//    At normal incidence both terms vanish and the image is exactly where it was. Off axis it
//    slides, bounded by the thickness. Measured on this panel, at the display's own resolution:
//
//        incidence     15°     30°     45°     60°     75°
//        shift       -0.71   -1.71   -3.55   -7.79  -21.84   display pixels
//
//    which is a displacement you can see, and see move, without being a distortion effect.
//
// 🔴 AND THERE IS NO DISPERSION HERE, BECAUSE IT WOULD BE A LIE.
//
//    The first version of this sampled red, green and blue separately, since the index of
//    refraction is wavelength dependent and the colour fringing at the rim of a thick cover is a
//    real thing. Then it got measured. Across crown glass's actual spread, n = 1.505 to 1.529,
//    the red and blue sample points differ by 0.12 of a pixel at sixty degrees and 0.17 at
//    seventy-five. It is below the grid. It cannot appear.
//
//    Two extra texture reads per pixel of the screen, every frame, to produce nothing that can be
//    resolved. You would need about two centimetres of cover glass before it crossed one pixel.
//    So it is gone, and this is one sample. If the thickness ever grows, measure it again.
fn GlassOffset(incident : vec3f, normal : vec3f, index : f32) -> vec2f {
  let bent = refract(incident, normal, 1.0 / index);
  if (dot(bent, bent) < 1.0e-8) { return vec2f(0.0); }       // total internal reflection
  let straightSlide = incident.xy / max(abs(incident.z), 1.0e-4);
  let bentSlide = bent.xy / max(abs(bent.z), 1.0e-4);
  return (bentSlide - straightSlide) * ${Glass.thickness};
}

@fragment
fn fsGlass(in : GlassVarying) -> @location(0) vec4f {
  let edge = max(fwidth(in.Local.x), fwidth(in.Local.y));
  let inside = CoverageFromDistance(
      DistanceRoundedRectangle(in.Local, R.face.xy - vec2f(0.0010), R.body.w * 0.86),
      max(edge, 1.0e-6));
  if (inside <= 0.0) { discard; }

  let normal = normalize(vec3f(R.rowX.z, R.rowY.z, R.rowZ.z));
  let view = normalize(G.eye.xyz - in.World);
  let facing = max(dot(normal, view), 0.0);

  // Into panel space, where the glass is the plane z = 0 and the refraction is two dimensional.
  let incident = normalize(RoomToPanelDirection(-view));
  let flat = vec3f(0.0, 0.0, 1.0);

  let base = DisplayTexture(in.Local);
  let slide = GlassOffset(incident, flat, ${Glass.index});
  let shifted = base + vec2f(slide.x / R.face.x * 0.5, -slide.y / R.face.y * 0.5);
  var transmitted = textureSample(DisplaySheet, DisplaySampler, shifted).rgb;

  // The glow, which is the whole reason a target was worth paying for. It is added inside the
  // glass, so it is attenuated by the same Fresnel term as everything else behind the surface and
  // it spills correctly into the darkened rim instead of floating above it.
  transmitted += textureSample(BloomSheet, DisplaySampler, base).rgb * ${Glass.bloomStrength};

  // 🔴 Fresnel decides the split, and it is a SPLIT. Straight on a coated screen reflects about
  //    four percent and you see the interface; at a glancing angle it reflects most of the light
  //    and the room wins. The old additive pass could only add the reflection, so the interface
  //    stayed at full strength underneath and the tablet never actually turned into a mirror.
  //    Energy has to leave the transmitted term for the reflected one.
  let fresnel = RoomSchlick(facing, 0.042);
  let sharpness = clamp(1.0 - R.tone.y, 0.0, 1.0);
  let bounce = reflect(-view, normal);
  var reflected = RoomLight(bounce, sharpness);
  reflected += RoomLight(bounce, sharpness * 0.35) * 0.20;

  var colour = transmitted * (1.0 - fresnel) + reflected * fresnel;

  // The lip, where the cover glass meets the body. This is the detail that separates a screen set
  // INTO something from a screen printed ON it.
  let lip = DistanceRoundedRectangle(in.Local, R.face.xy - vec2f(0.0010), R.body.w * 0.86);
  colour += vec3f(0.35, 0.40, 0.50) * exp(lip / 0.0016) * 0.55 * (0.25 + 0.75 * fresnel);

  return vec4f(colour * inside, inside);
}
`;
