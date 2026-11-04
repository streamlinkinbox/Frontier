
#pragma once
#include "IntrinsicParity.slang"
#include "NativeTwilight.slang"
#include "NightStars.slang"
// Analytic primary-ray celestial layer; intentionally excluded from diffuse GI LUTs.
float SkyHash(float3 P) { return frac(sin(dot(P, float3(127.1,311.7,74.7))) * 43758.5453); }
// 📝 DiscWeight scales the solar disc alone (RefinementSolarDisc: 0 in reflection rays, whose sun the direct term carries).
float3 ResolveCelestialRadiance(float3 D, Texture2DArray<float4> Atlas, SamplerState AtlasSampler, float DiscWeight) {
    let Sun = Refresh.SolarDisc.xyz;
    let Angle = acos(clamp(dot(D, Sun), -1.0, 1.0));
    let Coverage = max(0.00005, Refresh.StarControl.x);
    let Radius = Refresh.SolarDisc.w;
    let Limb = sqrt(max(0.0, 1.0 - pow(Angle / Radius, 2.0)));
    let Day = smoothstep(-0.02, 0.02, Sun.z);
    var Result = Refresh.SolarRadiance.rgb * Refresh.CelestialControl.z * Day * (1.0-smoothstep(Radius-Coverage,Radius+Coverage,Angle)) * (0.4+0.6*Limb) * 2500.0 * DiscWeight;
    Result += Refresh.SolarRadiance.rgb*Refresh.CelestialControl.z*Refresh.SolarRadiance.w*(exp(-Angle*40.0)*0.35+exp(-Angle*9.0)*0.03+exp(-Angle*2.5)*0.004);
    Result += NativeTwilightLine(D,Sun,Refresh.Twilight.x,Refresh.Twilight.y,Refresh.Twilight.z)*Refresh.Twilight.w;
    let Night = (1.0-smoothstep(-12.0, -2.0, Refresh.Night.w)) * smoothstep(0.0,0.12,D.z);
    Result+=ResolveNightStars(D,Coverage);

    // Native MoonAlong: sphere normal -> roll, axial tilt, spin -> sRGB atlas.
    for (var I = 0u; I<4u; I++) {
        let M = Refresh.MoonBodies[I];let Md = M.Direction.xyz;let RadiusM = max(1e-6,M.Params.x);
        if (M.Params.y<=0.0 || D.z<=0.0 || Md.z<=0.0){continue;}
        let A = acos(clamp(dot(D,Md),-1.0,1.0));
        if (A<RadiusM+Coverage) {
            let Axis = SelectBranch(float3(0.0,0.0,1.0),float3(0.0,1.0,0.0),abs(Md.z)>0.9998);
            let U = normalize(cross(Md,Axis));let V = cross(U,Md);
            let LC = float2(dot(D,U),dot(D,V))/max(1e-6,sin(RadiusM));
            let Z = sqrt(max(0.0,1.0-dot(LC,LC)));
            let CR = cos(M.Direction.w);let SR = sin(M.Direction.w);
            let N = float3(LC.x*CR-LC.y*SR,LC.x*SR+LC.y*CR,Z);
            let CT = cos(M.Surface.y);let ST = sin(M.Surface.y);
            let NT = float3(N.x,N.y*CT-N.z*ST,N.y*ST+N.z*CT);
            let UV = float2(frac((atan2(NT.x,NT.z)+M.Surface.x)/6.2831853+0.5),0.5-asin(clamp(NT.y,-1.0,1.0))/3.14159265);
            let Albedo = pow(Atlas.SampleLevel(AtlasSampler, float3(UV, float(ToSigned(M.Atlas.x))), 0.0).rgb,float3(M.Surface.w));
            // Authored phase is days since new; native reference's full-at-zero convention is converted here.
            let Wrap = M.Surface.z*0.3;
            let Lit = max(0.0,(dot(N,float3(sin(M.Params.z),0.0,-cos(M.Params.z)))+Wrap)/(1.0+Wrap));
            let Limb = lerp(1.0,0.5,pow(1.0-Z,2.0)*max(M.Surface.z,0.35));
            let Edge = 1.0-smoothstep(RadiusM*lerp(0.975,0.88,M.Surface.z)-Coverage,RadiusM*(1.0+M.Surface.z*0.06)+Coverage,A);
            let Rim = pow(max(0.0,1.0-Z),3.0)*M.Surface.z*0.08;
            Result*=1.0-Edge; // Opaque lunar disc occludes background stars.
            Result+=(Albedo*(pow(Lit,0.8)*Limb+0.012)+Rim)*M.Params.y*Edge*M.Atlas.yzw;
        }
        let Corona = (exp(-A/(RadiusM*1.4))*0.09+exp(-A*3.5)*0.005)*M.Params.w;
        Result+=float3(0.7,0.8,1.0)*Corona*M.Params.y*(0.4+0.6*(0.5-0.5*cos(M.Params.z)))*M.Atlas.yzw;
    }
    if (Refresh.CelestialControl.w>0.0 && Night>0.0) {
        let Tints : float3[4] = {float3(1.0,0.95,0.8),float3(1.0,0.85,0.65),float3(1.0,0.45,0.22),float3(0.85,0.78,0.55)};
        let Gains : float[4] = {2.5,1.5,0.7,0.5};
        for (var I = 0u; I<4u; I++) {
            let AngleP = acos(clamp(dot(D,Refresh.Planets[I].xyz),-1.0,1.0));
            let SizeP = max(Coverage,0.00012);
            Result+=Tints[I]*exp(-pow(AngleP/SizeP,2.0))*Gains[I]*Night*Refresh.CelestialControl.w*smoothstep(0.0,0.15,Refresh.Planets[I].z);
        }
    }
    return Result;
}

float3 CelestialSky(float3 D, Texture2DArray<float4> Atlas, SamplerState AtlasSampler) {
    return ResolveCelestialRadiance(D, Atlas, AtlasSampler, 1.0);
}
