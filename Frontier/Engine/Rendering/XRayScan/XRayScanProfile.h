/*==========================================================================================================================================
                                                         XRAYSCANPROFILE.H
==========================================================================================================================================*/
// 🧩 What the shaders read of the scan: the layer profile (the reference's uniforms for the ghost, wireframe, curtain and particles, in the car's
//    frame) and the view constants (the plane of the travelling front, the fade, the scanned occupants). Ports of EncodeScanRecord (GarageView.js)
//    and of the scan fields of FrameRecord (SceneRecords.wgsl).

#pragma once

#include "XRayScanSequence.h"

#include <array>
#include <cstdint>
#include <span>

namespace Frontier::XRayScan
{

inline constexpr size_t ScanProfileBytes = 192;   // [B] XRayScanProfile in XRayScanLayouts.slang
inline constexpr size_t ScanViewBytes    = 80;    // [B] XRayScanView in XRayScanOpacity.slang
inline constexpr float  ReferenceGround  = 0.62f; // [m] the reference's ground lies this far below its world's origin (GarageView.js)

// 📝 The car's frame on the ground: the point under the middle of the car and its three unit axes.
struct ScanStage
{
    std::array<double, 3> Origin  = {0.0, 0.0, 0.0};   // [m]
    std::array<double, 3> Forward = {1.0, 0.0, 0.0};   // [-]
    std::array<double, 3> Right   = {0.0, -1.0, 0.0};  // [-]
    std::array<double, 3> Up      = {0.0, 0.0, 1.0};   // [-]
};

// 📝 ScanRecord (ScanOverlay.wgsl): 12 float4. HudLineTop (Front.w) and the HUD flag (Layers.w) belong to the stage veils and the HUD canvas, which
//    are interface layers and not part of this module; they keep their places so the profile is the demo's record byte for byte.
[[nodiscard]] std::array<float, ScanProfileBytes / 4> EncodeScanProfile(const XRayScanSequence& Scan, const ScanStage& Stage, uint32_t SparkleCount,
                                                                        double HudLineTop = 0.0);

// 📝 The scan fields of the demo's FrameRecord, in a record of their own: ScanPlane (xyz the front's unit normal, w the offset), ScanState (x
//    the fade amount), the 128-bit mask of scanned occupants, the families the scan spares, and the camera's near plane for the depth test.
struct XRayScanView
{
    std::array<float, 4>    ScanPlane{};          // [-] dot(P, xyz) + w is the distance past the front [m]
    std::array<float, 4>    ScanState{};          // x amount
    std::array<uint32_t, 4> ScannedOccupants{};   // [-] bit i: occupant i is faded by the scan
    std::array<uint32_t, 4> SparedFamilies{};     // [-] x headlight, y taillight
    std::array<float, 4>    Camera{};             // x near plane [m]
};
static_assert(sizeof(XRayScanView) == ScanViewBytes, "XRayScanView must match XRayScanOpacity.slang");

// 📝 The mask of the occupants the scan fades (Indices ≥ 128 are ignored, as GarageView's EncodeOccupantMask).
[[nodiscard]] std::array<uint32_t, 4> EncodeOccupantMask(std::span<const uint32_t> Indices);

// 📝 ScanPlane = (stage forward, −forward·origin − front): the front sits Front metres along the car from its middle, and the plane carries it
//    in world space so the sweep follows the car wherever it stands.
[[nodiscard]] XRayScanView EncodeScanView(const XRayScanSequence& Scan, const ScanStage& Stage, std::span<const uint32_t> ScannedOccupants,
                                          double CameraNear);

} // namespace Frontier::XRayScan
