//============================================================================================================================================
//                                                          XRAYSCANOPACITY.SLANG
//============================================================================================================================================
// 🧩 What one view needs of the scan to fade the solid car: the plane of the travelling front, the fade amount and the occupants the scan
//    names. A port of the scan functions of ViewWorld.wgsl and of the two places the demo's solid passes apply them (the surface raster's
//    stochastic fade and the ray traversal's skip), which the illumination port calls when the scan is bound to its refresh.

#pragma once
#include "IntrinsicParity.slang"

// 📝 The scan fields of the demo's FrameRecord, in a profile of their own: the engine's RefreshConstants carries none of them, and the illumination
//    port's layouts are not changed for a visual effect. XRayScanProfile.h encodes it (EncodeScanView).
struct XRayScanView
{
    float4 ScanPlane;           // [-]  xyz the front's unit normal, w its offset: the distance past the front is dot(P, xyz) + w [m]
    float4 ScanState;           // [-]  x the fade amount
    uint4  ScannedOccupants;    // [-]  128 bits, bit i set: occupant i is faded by the scan
    uint4  SparedFamilies;      // [-]  x the headlight family, y the taillight family
    float4 Camera;              // [m]  x the near plane (the depth test of the overlay layers)
};

// 📝 The scan fades only the occupants the view names, and only while it has some amount.
bool OccupantScanned(XRayScanView View, uint Occupant)
{
    return View.ScanState.x > 0.0 && Occupant < 128u && ((View.ScannedOccupants[Occupant >> 5u] >> (Occupant & 31u)) & 1u) != 0u;
}

// 📝 The reference leaves its lamps untouched by the scan ("small emissive lamp (unpatched, stays solid)"): the car's head and tail lamps
//    keep their solid shading and have no ghost. The families ride in the view because the illumination port's host constants are not extended.
bool ScanSparesFamily(XRayScanView View, uint Family)
{
    return Family == View.SparedFamilies.x || Family == View.SparedFamilies.y;
}

// 📐 The scan's solid fade, Truck.tsx makeScannable() verbatim: sd = P.x − uScanX is the distance past the travelling front (positive where it has
//    already passed), sf = smoothstep(−0.9, −0.05, sd) and the surface keeps 1 − sf · uHoloAmt · 0.965 of its opacity. The plane carries the front
//    in world space, so the sweep follows the car wherever it stands.
float ResolveFrontDistance(XRayScanView View, float3 Position)
{
    return dot(Position, View.ScanPlane.xyz) + View.ScanPlane.w;
}

float ResolveScanOpacity(XRayScanView View, float3 Position)
{
    return 1.0 - smoothstep(-0.9, -0.05, ResolveFrontDistance(View, Position)) * View.ScanState.x * 0.965;
}

// 📐 The surface raster's hook (SurfaceRaster.wgsl): a scanned occupant's fragment is dropped with probability 1 − opacity against an interleaved
//    gradient that changes every refresh, so temporal antialiasing averages the coin flips into the fade. Pixel is the fragment's window position.
bool ResolveScanFadeDiscard(XRayScanView View, uint Occupant, uint Family, float3 WorldPosition, float2 Pixel, uint RefreshIndex)
{
    if (OccupantScanned(View, Occupant) && !ScanSparesFamily(View, Family))
    {
        let ScanPixel = Pixel + float2(5.588238 * float(RefreshIndex % 32u));
        let ScanThreshold = frac(52.9829189 * frac(dot(ScanPixel, float2(0.06711056, 0.00583715))));
        if (ResolveScanOpacity(View, WorldPosition) <= ScanThreshold)
        {
            return true;
        }
    }
    return false;
}

// 📐 The ray traversal's hook (RayTraversal.wgsl): a ray passes through a scanned triangle whose centroid has faded below half opacity, so the
//    ghosted car casts no shadow and shows what lies behind it to every traced ray. Centroid is the triangle's centre in world space.
bool ResolveScanRayPasses(XRayScanView View, bool Scanned, uint Family, float3 Centroid)
{
    return Scanned && !ScanSparesFamily(View, Family) && ResolveScanOpacity(View, Centroid) < 0.5;
}
