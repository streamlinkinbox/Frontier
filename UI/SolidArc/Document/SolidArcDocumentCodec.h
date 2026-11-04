/*====================================================================================================================================
                                                       SOLIDARCDOCUMENTCODEC.H
====================================================================================================================================*/
// 🧩 SolidArc document files (.solidarc): parameters, steps, sketches, references, captions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcDocument.h"

#include <map>
#include <string>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SolidArcDocumentFile
{
    std::string                        Title    = {};                                        // [-] - Document caption
    GeometryProcessing::FeatureTree              Tree     = {};                                        // [-] - Steps + parameters (signatures re-captured on load)
    std::map<std::string, std::string> Captions = {};                                        // [-] - Step label → caption
    std::vector<SolidArcImagePlane>    ImagePlanes = {};                                     // [-] - Blueprint references
    SolidArcAppearance                 Appearance  = {};                                     // [-] - Body / face matcaps
    SolidArcDrawingSheetSettings       DrawingSheet = {};                                    // [-] - Alt+D sheet settings
    std::vector<SolidArcDimension>     Dimensions   = {};                                    // [-] - Persistent 3D CAD dimension annotations
    std::string                        Refusal  = {};                                        // [-] - Parse failure (empty: fine)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The text is deterministic for one tree (keys in a fixed order), so a save → load → save round trip is byte-equal.
[[nodiscard]] std::string           EncodeSolidArcDocument(const SolidArcDocument& Document);
[[nodiscard]] SolidArcDocumentFile DecodeSolidArcDocument(const std::string& Text);

// File helpers: false / Refusal on an I/O or parse failure.
bool                               SaveSolidArcDocument(const SolidArcDocument& Document, const std::string& Path);
[[nodiscard]] SolidArcDocumentFile LoadSolidArcDocumentFile(const std::string& Path);

} // namespace Frontier::SolidArc
