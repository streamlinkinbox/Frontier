
#pragma once
#include "IntrinsicParity.slang"
// Common-domain physical medium and LUT coordinate contract. Units: metres, inverse metres.
struct AtmosphereConstants
{
    float3 SunDirection;        // [-]  unit, toward the sun
    float SolarIlluminance;      // [-]  top-of-atmosphere illuminance scale
    float ObserverAltitude;      // [m]
    float Pad0;             
    float Pad1;             
    float Pad2;             
    float4 Medium; 
    float4 Heights; 
    float4 SolarTint; 
    float4 Ground; 
};

  [[vk::binding(0, 0)]] ConstantBuffer<AtmosphereConstants> Atmosphere;


static const float GroundRadius = 6360000.0;                                  // [m]
static const float AtmosphereRadius = 6460000.0;                                  // [m]
static const float3 RayleighScatter = float3(5.802e-6, 13.558e-6, 33.1e-6);       // [1/m]
static const float RayleighHeight = 8000.0;                                     // [m]
static const float MieScatter = 3.996e-6;                                   // [1/m]
static const float MieExtinction = 4.40e-6;                                    // [1/m]
static const float MieHeight = 1200.0;                                     // [m]
static const float MieAsymmetry = 0.8;                                        // [-]
static const float3 OzoneAbsorption = float3(0.650e-6, 1.881e-6, 0.085e-6);       // [1/m]
static const uint ViewSamples = 40u;
static const uint LightSamples = 64u;

// 📝 Ray–sphere intersection about the planet centre; returns (near, far), far < 0 when the sphere is missed.
float2 IntersectSphere(float3 Origin, float3 Direction, float Radius)
{
    let b = dot(Origin, Direction);
    let c = dot(Origin, Origin) - Radius * Radius;
    let Discriminant = b * b - c;
    if (Discriminant < 0.0)
    {
        return float2(-1.0, -1.0);
    }
    let Root = sqrt(Discriminant);
    return float2(-b - Root, -b + Root);
}

float3 ResolveDensities(float Altitude)
{
    let Ozone = max(0.0, 1.0 - abs(Altitude - 25000.0) / 15000.0);
    return float3(exp(-Altitude / Atmosphere.Heights.x), exp(-Altitude / Atmosphere.Heights.y), Ozone);
}

float3 ResolveExtinction(float3 Densities)
{
    return RayleighScatter * Atmosphere.Medium.x * Densities.x + float3(MieExtinction * Atmosphere.Medium.y * Densities.y) + OzoneAbsorption * Atmosphere.Medium.z * Densities.z;
}

float3 MarchTransmittance(float3 Position, float3 Sun)
{
    if (IntersectSphere(Position, Sun, GroundRadius).x > 0.0)
    {
        return float3(0.0);
    }
    let Exit = IntersectSphere(Position, Sun, AtmosphereRadius).y;
    let Step = Exit / float(LightSamples);
    var Depth = float3(0.0);
    for (var Sample = 0u; Sample < LightSamples; Sample++)
    {
        let Point = Position + Sun * ((float(Sample) + 0.5) * Step);
        Depth += ResolveExtinction(ResolveDensities(length(Point) - GroundRadius)) * Step;
    }
    return exp(-Depth);
}


float2 TransmittanceUV(float3 P, float3 Sun) {
 return float2(dot(normalize(P),Sun)*0.5+0.5,sqrt(clamp((length(P)-GroundRadius)/(AtmosphereRadius-GroundRadius),0.0,1.0)));
}
float3 LookupTransmittance(Texture2D<float4> Tex, SamplerState S, float3 P, float3 Sun) {
 if (IntersectSphere(P,Sun,GroundRadius).x>0.0) { return float3(0.0); }
 return Tex.SampleLevel(S, TransmittanceUV(P,Sun), 0.0).rgb;
}
float3 LookupMultiple(Texture2D<float4> Tex, SamplerState S, float3 P, float3 Sun) {
 return Tex.SampleLevel(S, TransmittanceUV(P,Sun), 0.0).rgb;
}
