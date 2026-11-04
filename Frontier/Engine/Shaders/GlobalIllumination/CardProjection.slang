//============================================================================================================================================
//                                                          CARDPROJECTION.SLANG
//============================================================================================================================================
// 🧩 The one mapping between an occupant's local refresh and a card's (u, v, depth) — common-domain by capture and every lookup.

#pragma once
#include "IntrinsicParity.slang"

#include "SceneLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                   CARD PROJECTION
//------------------------------------------------------------------------------------------------------------------------

float3 ResolveCardAxis(uint Axis)
{
    var Outward = float3(0.0);
    Outward[Axis >> 1u] = SelectBranch(1.0, -1.0, (Axis & 1u) == 1u);
    return Outward;
}

// 📝 Card coordinates of a local point: u, v across the face, depth along the inward view direction, all in [0, 1].
//    Depth axis k = Axis/2, u axis (k+1) mod 3, v axis (k+2) mod 3 — capture and lookup share this one mapping.
float3 ProjectCardCoordinates(CardProfile Card, float3 Local)
{
    let DepthAxis = Card.Axis >> 1u;
    let AcrossU = (DepthAxis + 1u) % 3u;
    let AcrossV = (DepthAxis + 2u) % 3u;
    let Extent = max(Card.LocalMaximum - Card.LocalMinimum, float3(1.0e-5));
    let U = (Local[AcrossU] - Card.LocalMinimum[AcrossU]) / Extent[AcrossU];
    let V = (Local[AcrossV] - Card.LocalMinimum[AcrossV]) / Extent[AcrossV];
    let FromMaximum = (Card.LocalMaximum[DepthAxis] - Local[DepthAxis]) / Extent[DepthAxis];
    let FromMinimum = (Local[DepthAxis] - Card.LocalMinimum[DepthAxis]) / Extent[DepthAxis];
    return float3(U, V, SelectBranch(FromMinimum, FromMaximum, (Card.Axis & 1u) == 0u));
}

float3 ReconstructCardLocal(CardProfile Card, float3 Coordinates)
{
    let DepthAxis = Card.Axis >> 1u;
    let AcrossU = (DepthAxis + 1u) % 3u;
    let AcrossV = (DepthAxis + 2u) % 3u;
    let Extent = Card.LocalMaximum - Card.LocalMinimum;
    var Local = float3(0.0);
    Local[AcrossU] = Card.LocalMinimum[AcrossU] + Coordinates.x * Extent[AcrossU];
    Local[AcrossV] = Card.LocalMinimum[AcrossV] + Coordinates.y * Extent[AcrossV];
    Local[DepthAxis] = SelectBranch(Card.LocalMinimum[DepthAxis] + Coordinates.z * Extent[DepthAxis],
                              Card.LocalMaximum[DepthAxis] - Coordinates.z * Extent[DepthAxis], (Card.Axis & 1u) == 0u);
    return Local;
}

float ResolveCardDepthExtent(CardProfile Card)
{
    let Extent = Card.LocalMaximum - Card.LocalMinimum;
    return max(Extent[Card.Axis >> 1u], 1.0e-4);
}
