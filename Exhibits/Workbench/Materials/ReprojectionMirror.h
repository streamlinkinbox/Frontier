//============================================================================================================================================
//                                                    REPROJECTIONMIRROR.H
//============================================================================================================================================
// 🧩 The M9 reprojection rule, mirrored from Engine/Shaders/ReSTIRViewport.slang `ResolveSurface` (lines pinned by the
//    proof's §B text audit and exercised by §D). Shared by two callers:
//      · Exhibits/Workbench/Materials/DenoiseReprojectionProof.cpp — the §D rule checks;
//      · Exhibits/Workbench/Materials/DenoiseExhibit.cpp         — the temporal A/B sheets, which need the SAME rule
//                                                                  the kernel applies, not a look-alike.
//
//    What the shader does, in order: background (depth ≤ 0) and frames with the feature bit clear never reproject;
//    otherwise back-project through the R2 motion vector, and inherit the previous frame's mean + moments only if the
//    pixel that is found held a surface with a normal within 25° and a depth within 10 %. Everything else — off screen,
//    no surface, normal or depth mismatch — is a disocclusion and leaves the count at 0, i.e. the mean restarts.
//
//    The FEATURE-OFF path is not a disocclusion in the shader: `!resolved` reads the pixel's OWN history and keeps its
//    count (the pre-R7a accumulator, byte-identical). That is the caller's half of the contract, so `Inherited` here
//    means "history found and validated at the reprojected site" — a caller with the feature clear uses its own pixel
//    regardless (ReprojectionAnswer::PreviousX/Y already point there).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

struct HistoryTexel
{
    float Normal[3] = { 0.0f, 0.0f, 1.0f };
    float Depth     = -1.0f;          // ≤ 0 = no surface (background, direct emitters)
    float Count     = 0.0f;
};

struct ReprojectionAnswer
{
    int  PreviousX = 0;
    int  PreviousY = 0;
    bool Inherited = false;          // true = history found and validated; false = disocclusion (restart at n = 1)
    bool Offscreen = false;
};

constexpr float kReprojectNormalCos = 0.906307787f;   // 25° — kTemporalNormalCos, shared with reservoir reuse
constexpr float kReprojectDepthTol  = 0.10f;

inline ReprojectionAnswer Reproject(float PixelX, float PixelY, uint32_t Width, uint32_t Height,
                                    const float Normal[3], float Depth, float MotionU, float MotionV,
                                    const HistoryTexel* History, bool FeatureEnabled)
{
    ReprojectionAnswer Answer;
    Answer.PreviousX = static_cast<int>(PixelX);
    Answer.PreviousY = static_cast<int>(PixelY);

    const bool Reprojecting = Depth > 0.0f && FeatureEnabled;
    if (!Reprojecting) return Answer;   // pre-R7a behaviour: own pixel, and the answer says so

    const float CentreU = (PixelX + 0.5f) / static_cast<float>(Width);
    const float CentreV = (PixelY + 0.5f) / static_cast<float>(Height);
    const float PrevU   = CentreU - MotionU;
    const float PrevV   = CentreV - MotionV;
    const int   PrevX   = static_cast<int>(std::floor(PrevU * static_cast<float>(Width)));
    const int   PrevY   = static_cast<int>(std::floor(PrevV * static_cast<float>(Height)));

    if (PrevX < 0 || PrevY < 0 || PrevX >= static_cast<int>(Width) || PrevY >= static_cast<int>(Height))
    {
        Answer.Offscreen = true;
        return Answer;                  // back-projected off screen — also a disocclusion
    }

    Answer.PreviousX = PrevX;
    Answer.PreviousY = PrevY;

    const HistoryTexel& Tap = History[static_cast<size_t>(PrevY) * Width + static_cast<size_t>(PrevX)];
    const float Dot = Normal[0] * Tap.Normal[0] + Normal[1] * Tap.Normal[1] + Normal[2] * Tap.Normal[2];
    const float DepthDelta = std::fabs(Depth - Tap.Depth) / std::max(Depth, 1.0e-3f);
    Answer.Inherited = Tap.Depth > 0.0f && Dot > kReprojectNormalCos && DepthDelta < kReprojectDepthTol;
    return Answer;
}
