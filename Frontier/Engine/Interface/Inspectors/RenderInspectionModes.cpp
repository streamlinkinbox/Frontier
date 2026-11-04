/*==========================================================================================================================================
                                                         RENDERINSPECTIONMODES.CPP
==========================================================================================================================================*/
// 🧩 The renderer's debug views by the names the Render settings page and the viewport bar list: the demo's inspection views 0–14
//    (RefreshToggles::Inspection) and, as view 15, the editor's analytic illumination; plus what view 6 reads.

#include "Engine/Interface/Inspectors/RenderInspectionModes.h"

namespace Frontier
{

// 📝 The VehicleHMI demo's inspection views (InspectorPages.js), in its order: the index is RefreshToggles::Inspection and the levelspace
//    inspection_mode. View 15 is not an inspection mode: it swaps the global illumination for the analytic illumination.
const RenderInspectionView RenderInspectionModes::Views[RenderInspectionModes::ViewCount] = {
    {"Lighting", "00", "Final image", "", false},
    {"Lighting", "01", "Direct sun only", "Sun and sky light without the indirect bounce", false},
    {"Lighting", "02", "Indirect diffuse (GI)", "The bounce light the surfaces reflect", false},
    {"Lighting", "03", "Indirect irradiance (denoised)", "After the diffuse variance filter", false},
    {"Lighting", "04", "ReSTIR irradiance (raw)", "Before denoising: what the paths found this refresh", false},
    {"Lighting", "05", "Glossy reflections", "The reflection lobe as the recombination uses it", false},
    {"Scene", "06", "BVH / distance field", "Primary rays through the BVH (RTX) or the distance field (GTX) alone", false},
    {"Scene", "07", "Surface cards (texel space)", "The surface cache the GTX path and the cards light", false},
    {"Scene", "08", "Normals", "", false},
    {"Scene", "09", "Albedo", "", false},
    {"Clusters", "10", "Visibility buffer IDs", "One colour per cluster", true},
    {"Clusters", "11", "Deferred resolve", "Materials resolved from the visibility buffer", true},
    {"Clusters", "12", "Hi-Z pyramid", "", false},
    {"Clusters", "13", "Glass layers (front / back)", "", false},
    {"Clusters", "14", "LOD levels (near to far)", "", false},
    {"Fast preview", "AI", "Analytic illumination", "One shadowed sun, Lambert + Blinn-Phong, hemisphere fill: no rays", false},
};

// 📝 DistanceInspection (the demo's InspectionMarch): 1 field normals by cascade, 2 traversal steps, 3 card or next-event radiance.
const char* const RenderInspectionModes::DistanceNames[RenderInspectionModes::DistanceCount] = {
    "Normals",
    "Step heat",
    "NEE / cards",
};

} // namespace Frontier
