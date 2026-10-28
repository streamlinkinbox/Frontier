//============================================================================================================================================
//                                                      CLUSTERDEBUGFALLBACK.WGSL
//============================================================================================================================================
// 🧩 Placeholders written into the display inspection target when a cluster debug view (Display modes
//    10-13) is selected but the virtualized runtime cannot show real data yet. The checker colour IS
//    the diagnosis, readable from a screenshot without opening the console:
//      magenta — runtime still activating (programs/targets being built), should clear within a second;
//      red     — activation FAILED; the status badge (top-left) and console carry the exact error;
//      orange  — runtime is up but the scene has no eligible facet set (>= 256 facets, static,
//                non-breakable), so there is no virtualized geometry to inspect.
//    A genuinely empty visibility buffer instead reads as a dark-blue field from the debug entries
//    themselves, so "no coverage" never masquerades as a dead view either.

@group(0) @binding(0) var Target : texture_storage_2d<rgba16float, write>;

fn PaintChecker(ID : vec3u, Dark : vec3f, Bright : vec3f)
{
    let Extent = textureDimensions(Target);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Checker = ((ID.x / 24u) + (ID.y / 24u)) % 2u == 0u;
    textureStore(Target, vec2i(ID.xy), vec4f(select(Dark, Bright, Checker), 1.0));
}

@compute @workgroup_size(8, 8)
fn DebugUnavailable(@builtin(global_invocation_id) ID : vec3u) { PaintChecker(ID, vec3f(0.10, 0.02, 0.14), vec3f(0.62, 0.08, 0.52)); }

@compute @workgroup_size(8, 8)
fn DebugFailed(@builtin(global_invocation_id) ID : vec3u) { PaintChecker(ID, vec3f(0.16, 0.02, 0.02), vec3f(0.66, 0.10, 0.08)); }

@compute @workgroup_size(8, 8)
fn DebugNoGeometry(@builtin(global_invocation_id) ID : vec3u) { PaintChecker(ID, vec3f(0.16, 0.07, 0.01), vec3f(0.68, 0.38, 0.06)); }
