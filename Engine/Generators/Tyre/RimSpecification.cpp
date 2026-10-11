//============================================================================================================================================
//                                                          RIMSPECIFICATION.CPP
//============================================================================================================================================
// 📦 The rim material list and the bead-seat profile the barrel is lathed from.

#include "RimSpecification.h"

#include <algorithm>

namespace Frontier {

const std::vector<RimMaterial>& RimMaterials()
{
    static const std::vector<RimMaterial> Materials = {
        { "Steel",          0xFF9EA5ADu, 0.88f, 0.30f },
        { "Maraging steel", 0xFF66717Bu, 0.91f, 0.20f },
        { "Cast steel",     0xFF51555Au, 0.72f, 0.47f },
        { "Aluminium",      0xFFCFD5D9u, 0.90f, 0.23f },
        { "Brass",          0xFFB88943u, 0.82f, 0.28f }
    };
    return Materials;
}

std::vector<RimProfilePoint> RimProfilePoints(const RimSpecification& Rim, const TreadDerivedValues& Derived)
{
    // ① the four numbers the whole profile is written in terms of
    const float Half = Derived.BeadHalf + 3.0f;
    const float Seat = Derived.RimRadius + 2.0f;
    const float Lip  = Seat + std::max(4.0f, Rim.Flange);
    const float Well = Seat - std::max(3.0f, Rim.DropWell);
    constexpr float Wall = 3.5f;   // [mm] - barrel thickness, constant the whole way round

    // ② out along the front face, down into the well, up the back face, then home at one wall thickness
    return {
        { -Half,         Lip - 1.0f },        { -Half - 1.0f,  Lip + 1.0f },
        { -Half + 2.0f,  Lip + 1.0f },        { -Half + 3.5f,  Seat + 3.0f },
        { -Half + 7.0f,  Seat },              { -Half + 15.0f, Seat - 1.0f },
        { -Half + 21.0f, Well },              {  Half - 21.0f, Well },
        {  Half - 15.0f, Seat - 1.0f },       {  Half - 7.0f,  Seat },
        {  Half - 3.5f,  Seat + 3.0f },       {  Half - 2.0f,  Lip + 1.0f },
        {  Half + 1.0f,  Lip + 1.0f },        {  Half,         Lip - 1.0f },

        {  Half,         Lip - 1.0f - Wall }, {  Half - 1.0f,  Lip + 1.0f - Wall },
        {  Half - 2.0f,  Lip + 1.0f - Wall }, {  Half - 3.5f,  Seat + 3.0f - Wall },
        {  Half - 5.0f,  Seat - Wall },       {  Half - 13.0f, Seat - Wall - 1.0f },
        {  Half - 20.0f, Well - Wall },       { -Half + 20.0f, Well - Wall },
        { -Half + 13.0f, Seat - Wall - 1.0f },{ -Half + 5.0f,  Seat - Wall },
        { -Half + 3.5f,  Seat + 3.0f - Wall },{ -Half + 2.0f,  Lip + 1.0f - Wall },
        { -Half - 1.0f,  Lip + 1.0f - Wall }, { -Half,         Lip - 1.0f - Wall }
    };
}

}   // namespace Frontier
