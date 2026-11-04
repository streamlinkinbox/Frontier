
#pragma once
#include "IntrinsicParity.slang"
// Common-domain cloud / bounded-fog ABI, 544 bytes. First eight vectors preserve display/shadow controls.
struct FogLamp {float4 PositionType;float4 DirectionReach;float4 ColourIntensity;float4 Params;};
struct CloudParameters {float4 Layer;float4 Shape;float4 Centre;float4 Size;float4 Local;float4 Control;float4 Wind;float4 Render;
 float4 Formation;float4 LocalShape;float4 Advection;float4 FogCentre;float4 FogSize;float4 FogMedium;float4 FogMaterial;float4 FogLighting;float4 FogWind;float4 FogControl;FogLamp Lamps[4];};
