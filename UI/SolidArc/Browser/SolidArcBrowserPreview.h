/*====================================================================================================================================
                                                      SOLIDARCBROWSERPREVIEW.H
====================================================================================================================================*/
// 🧩 Project browser preview of a .solidarc file: decode, regenerate, tessellate and place it in the browser frame

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcScene.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SolidArcBrowserPreview
{
    std::shared_ptr<const std::vector<float>> Triangles     = {};                            // [-]   - Browser vertex layout (10 floats), browser frame
    std::string                               Key           = {};                            // [-]   - Residence key: path, size and write time
    std::string                               Title         = {};                            // [-]   - Document caption
    size_t                                    StepCount     = 0;                             // [-]   - Feature steps in the tree
    size_t                                    BodyCount     = 0;                             // [-]   - Solid bodies after regeneration
    size_t                                    FaceCount     = 0;                             // [-]   - Faces after regeneration
    size_t                                    TriangleCount = 0;                             // [-]   - Document triangles (floor excluded)
    double                                    Milliseconds  = 0.0;                           // [ms]  - Decode + regenerate + tessellate
    std::string                               Refusal       = {};                            // [-]   - Why no preview exists (empty: fine)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The browser frame is Y-up with the reference camera aimed at (0, 1.3, 0): the Z-up scene is turned (x, y, z) → (x, z, −y),
//    scaled to the reference footprint (≤ 5.6 wide, ≤ 3.2 tall), centred on the floor and given the reference floor and grid.
//    Bodies cycle through the five reference surfaces so neighbouring bodies stay distinguishable.
[[nodiscard]] std::vector<float> ConvertSolidArcSceneToBrowserFrame(const SolidArcScene& Scene);

// Loads the file, replays its tree and tessellates the result (the same path as File ▸ Open in the workspace).
[[nodiscard]] SolidArcBrowserPreview ComposeSolidArcBrowserPreview(const std::filesystem::path& Path);

} // namespace Frontier::SolidArc
