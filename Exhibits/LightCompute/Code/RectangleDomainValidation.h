/*==========================================================================================================================================
                                                  RECTANGLEDOMAINVALIDATION.H
==========================================================================================================================================*/
// 🧩 Validate the narrow common-support assumptions of the Vulkan rectangle reconnection experiment.
#pragma once

namespace
{
struct RectangleDomain
{
    float MinX = 0, MaxX = 0, MinY = 0, MaxY = 0, Height = 0;
    std::array<float, 3> Emission{};
};
RectangleDomain ValidateRectangleDomain(const LightComputeArchive& Scene)
{
    Require(Scene.Surfaces.size() == 2 && Scene.Visibility.Corners.size() == 2 &&
                Scene.Visibility.Enclosures.size() == 1 && Scene.Lights.empty(),
            "Rectangle experiment requires exactly two emissive triangles and no analytic lights");
    RectangleDomain D;
    D.MinX = D.MinY = std::numeric_limits<float>::infinity();
    D.MaxX = D.MaxY = -std::numeric_limits<float>::infinity();
    D.Height = Scene.Surfaces[0].A[2];
    D.Emission = {Scene.Surfaces[0].Emissive[0], Scene.Surfaces[0].Emissive[1], Scene.Surfaces[0].Emissive[2]};
    for (float E : D.Emission) Require(std::isfinite(E) && E >= 0 && E <= 1e4f, "Invalid rectangle radiance");
    for (size_t I = 0; I < 2; ++I)
    {
        const auto& F = Scene.Surfaces[I];
        const auto& V = Scene.Visibility.Corners[I];
        Require(F.A == V.A && F.B == V.B && F.C == V.C && F.Emissive[3] == 0 && F.OpticalToken[0] == 0 &&
                    F.Emissive[0] == D.Emission[0] && F.Emissive[1] == D.Emission[1] &&
                    F.Emissive[2] == D.Emission[2], "Rectangle facets must match the BVH and uniform radiance");
        for (const auto* P : {&F.A, &F.B, &F.C})
        {
            Require(std::isfinite((*P)[0]) && std::isfinite((*P)[1]) && std::isfinite((*P)[2]) &&
                        std::abs((*P)[0]) <= 1e4f && std::abs((*P)[1]) <= 1e4f && std::abs((*P)[2]) <= 1e4f &&
                        (*P)[2] == D.Height, "Rectangle vertices must lie in one bounded horizontal plane");
            D.MinX = std::min(D.MinX, (*P)[0]); D.MaxX = std::max(D.MaxX, (*P)[0]);
            D.MinY = std::min(D.MinY, (*P)[1]); D.MaxY = std::max(D.MaxY, (*P)[1]);
        }
    }
    Require(D.MaxX - D.MinX >= .01f && D.MaxY - D.MinY >= .01f, "Degenerate rectangle");
    const auto& Box = Scene.Visibility.Enclosures[0];
    Require(Box.Links[0] == 0 && Box.Links[1] == 2 && Box.Links[2] == 1 &&
                Box.Minimum[0] <= D.MinX && Box.Minimum[1] <= D.MinY && Box.Minimum[2] <= D.Height &&
                Box.Maximum[0] >= D.MaxX && Box.Maximum[1] >= D.MaxY && Box.Maximum[2] >= D.Height,
            "Rectangle BVH must enclose both facets in one leaf");
    uint32_t Masks[2]{};
    for (size_t I = 0; I < 2; ++I)
    {
        const auto& F = Scene.Surfaces[I];
        for (const auto* P : {&F.A, &F.B, &F.C})
        {
            Require(((*P)[0] == D.MinX || (*P)[0] == D.MaxX) && ((*P)[1] == D.MinY || (*P)[1] == D.MaxY),
                    "Only axis-aligned rectangle corners are supported");
            const uint32_t Corner = uint32_t((*P)[0] == D.MaxX) + 2u * uint32_t((*P)[1] == D.MaxY);
            Require((Masks[I] & (1u << Corner)) == 0, "Degenerate rectangle triangle");
            Masks[I] |= 1u << Corner;
        }
    }
    const uint32_t Shared = Masks[0] & Masks[1];
    Require((Masks[0] | Masks[1]) == 15 && (Shared == 9 || Shared == 6),
            "Triangles must cover the rectangle along a diagonal");
    for (const auto& R : Scene.Receivers)
    {
        Require(R.PositionHit[3] == 0 || R.PositionHit[3] == 1, "Invalid receiver hit flag");
        if (R.PositionHit[3] == 0) continue;
        Require(R.NormalRoughness[0] == 0 && R.NormalRoughness[1] == 0 && R.NormalRoughness[2] == 1 &&
                    R.ViewMetallic[3] == 0 && std::isfinite(R.MotionPositionToken[3]) && R.MotionPositionToken[3] > 0 &&
                    (R.MotionPixel[3] == 0 || R.MotionPixel[3] == 1) &&
                    std::isfinite(R.MotionPixel[0]) && std::isfinite(R.MotionPixel[1]),
                "Rectangle receivers must be identified nonmetallic +Z surfaces with finite motion");
        for (uint32_t I = 0; I < 3; ++I)
            Require(std::isfinite(R.PositionHit[I]) && std::abs(R.PositionHit[I]) <= 1e4f &&
                        std::isfinite(R.MotionPositionToken[I]) && std::abs(R.MotionPositionToken[I]) <= 1e4f &&
                        std::isfinite(R.AlbedoIor[I]) && R.AlbedoIor[I] >= 0 && R.AlbedoIor[I] <= 1,
                    "Invalid rectangle receiver");
        Require(R.PositionHit[2] + .01f <= D.Height, "Receiver must stay below the emitting plane");
    }
    return D;
}
bool CompatibleRectangles(RectangleDomain A, RectangleDomain B)
{
    return A.MaxX - A.MinX == B.MaxX - B.MinX && A.MaxY - A.MinY == B.MaxY - B.MinY &&
           A.Emission == B.Emission;
}
} // namespace
