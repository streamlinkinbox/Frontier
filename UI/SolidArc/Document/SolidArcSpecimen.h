/*====================================================================================================================================
                                                          SOLIDARCSPECIMEN.H
====================================================================================================================================*/
// 🧩 The bracket specimen: one modelling session (tiles, boolean, picks, fillet, chamfer) shared by validation and captures

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcDocument.h"

#include <string>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Block 4 × 2.5 × 1 ∪ boss (r 0.5, h 0.75) − through hole (r 0.3), fillet r 0.25 on the x = 4 top edge and chamfer
//    0.2 on the x = 0 top edge (both picked by rays, as a click would). Refusals of any command are appended to Notes.
[[nodiscard]] SolidArcDocument BuildSolidArcSpecimen(std::vector<std::string>* Notes = nullptr);

} // namespace Frontier::SolidArc
