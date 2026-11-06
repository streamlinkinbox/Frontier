#!/usr/bin/env python3
"""
Compiles all Vulkan 1.1 GLSL 450 shaders for Frontier's PyroVolumeSolver, WorldSdfScene,
ControlVehicle .facet mesh renderer, and Dear ImGui presenter into validated SPIR-V uint32_t[] arrays.

Usage:
  python3 Tools/Build/GenerateVulkanShaders.py [--glslang /path/to/glslang] [--check]
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_HEADER = REPO_ROOT / "Frontier/Engine/Rendering/Shaders/VulkanSpirvLibrary.h"

# ---------------------------------------------------------------------------------------------------------------------
#                                        SHARED PYRO UNIFORM & ATLAS HELPERS
# ---------------------------------------------------------------------------------------------------------------------

PYRO_UBO_GLSL = r"""
layout(std140, set = 0, binding = 0) uniform PyroUniformBlock {
    vec4 uGridParams;           // x=gridRes, y=tilesX, z=tilesY, w=dt
    vec4 uTimeAndFlags;         // x=time, y=hasCurl, z=worldSdfEnabled, w=clearMode
    vec4 uClearColorVal;        // rgba
    vec4 uDomainBoxMin;         // xyz=boxMin, w=enclosedBox
    vec4 uDomainBoxMax;         // xyz=boxMax, w=aspectRatio
    vec4 uEmitterParams0;       // x=enabled, y=rate, z=radius, w=height
    vec4 uEmitterParams1;       // x=smoke, y=temp, z=fuel, w=upVel
    vec4 uEmitterParams2;       // x=swirl, y=brushActive, z=obstacleType, w=obstacleRadius
    vec4 uBrushPosAndPad;       // xyz=brushPos
    vec4 uBrushVelAndPad;       // xyz=brushVel
    vec4 uObstaclePosAndPad;    // xyz=obstaclePos
    vec4 uObstacleVelAndPad;    // xyz=obstacleVel
    vec4 uBlastCenterAndRadius; // xyz=blastCenter, w=blastRadius
    vec4 uBlastThermoParams;    // x=active, y=strength, z=temp, w=fuel
    vec4 uBlastExtraParams;     // x=smoke, y=lobes, z=seed, w=projCount
    vec4 uBlastDirVelAndPad;    // xyz=blastDirVel, w=extSourceCount
    vec4 uProjData[6];          // xyz=pos, w=radius
    vec4 uProjVel[6];           // xyz=vel
    vec4 uExtPosRad[8];         // xyz=uvw, w=radiusUvw
    vec4 uExtVelRate[8];        // xyz=velPyro, w=rate
    vec4 uExtThermo[8];         // x=smoke, y=temp, z=fuel
    vec4 uAdvectAndForces0;     // x=smokeDiss, y=tempCool, z=fuelDiss, w=velDamping
    vec4 uAdvectAndForces1;     // x=vorticity, y=turbStrength, z=turbScale, w=windX
    vec4 uAdvectAndForces2;     // x=windZ, y=burnRate, z=burnHeat, w=sootGen
    vec4 uAdvectAndForces3;     // x=buoyancy, y=smokeWeight, z=combExpansion, w=fireScatter
    vec4 uCamPosAndFov;         // xyz=camPos, w=tanHalfFov
    vec4 uCamForwardAndComp;    // xyz=camForward, w=compositeOverScene
    vec4 uCamRightAndTransp;    // xyz=camRight, w=transparentBg
    vec4 uCamUpAndChannel;      // xyz=camUp, w=renderChannel
    vec4 uSunDirAndIntensity;   // xyz=sunDir, w=sunIntensity
    vec4 uOpticsParams0;        // x=ambient, y=extinction, z=albedo, w=fireIntensity
    vec4 uOpticsParams1;        // x=tempScale, y=palette, z=internalScatter, w=shadowDensity
    vec4 uOpticsParams2;        // x=phaseAniso, y=exposure, z=bloom, w=godRays
    vec4 uOpticsParams3;        // x=shockwave, y=raySteps, z=shadowSteps, w=voxelQuant
    vec4 uSliceAndPipParams;    // x=sliceAxis, y=slicePos, z=showAtlasPip, w=atlasPipField
    vec4 uEmberAndActiveParams; // x=showActiveCells, y=emberSize, z=emberIntensity, w=emberLifetime
    vec4 uEmberExtraParams;     // x=emberAshiness, y=viewportHeightPx, z=nearPlane, w=farPlane
    vec4 uWorldGridOrigin;      // x=originX, y=originZ, z=priorOriginX, w=priorOriginZ
    vec4 uWorldGridFlags;       // x=worldGridEnabled, y=cellSizeXZ, z=cellSizeY, w=floorWorldZ
};

layout(set = 0, binding = 1) uniform sampler2D uTex0;
layout(set = 0, binding = 2) uniform sampler2D uTex1;
layout(set = 0, binding = 3) uniform sampler2D uTex2;
layout(set = 0, binding = 4) uniform sampler2D uTex3;
layout(set = 0, binding = 5) uniform sampler2D uTex4;

float gRes()   { return uGridParams.x; }
float gTilesX(){ return uGridParams.y; }
float gTilesY(){ return uGridParams.z; }
vec2  gAtlasSize() { return vec2(uGridParams.x * uGridParams.y, uGridParams.x * uGridParams.z); }
bool  isWorldGrid() { return uWorldGridFlags.x > 0.5; }
ivec2 worldGridOrigin() { return ivec2(int(round(uWorldGridOrigin.x)), int(round(uWorldGridOrigin.y))); }
ivec2 worldGridPriorOrigin() { return ivec2(int(round(uWorldGridOrigin.z)), int(round(uWorldGridOrigin.w))); }

ivec3 wrapAtlasVoxel(ivec3 wc, int N) {
    int wx = ((wc.x % N) + N) % N;
    int wy = clamp(wc.y, 0, N - 1);
    int wz = ((wc.z % N) + N) % N;
    return ivec3(wx, wy, wz);
}

ivec3 atlasPxToRawVoxel(ivec2 px) {
    int N  = int(uGridParams.x + 0.5);
    int tx = px.x / N;
    int ty = px.y / N;
    int z  = ty * int(uGridParams.y + 0.5) + tx;
    return ivec3(px.x - tx * N, px.y - ty * N, z);
}

ivec3 atlasPxToVoxel(ivec2 px) {
    int N = int(uGridParams.x + 0.5);
    ivec3 av = atlasPxToRawVoxel(px);
    if (!isWorldGrid() || av.z >= N) return av;
    ivec2 org = worldGridOrigin();
    int lx = (((av.x - org.x) % N) + N) % N;
    int lz = (((av.z - org.y) % N) + N) % N;
    return ivec3(lx, av.y, lz);
}

bool worldGridEnteredFresh(ivec3 localV, int N) {
    if (!isWorldGrid()) return false;
    ivec2 org  = worldGridOrigin();
    ivec2 porg = worldGridPriorOrigin();
    ivec2 wc   = org + localV.xz;
    return wc.x < porg.x + 1 || wc.x >= porg.x + N - 1 ||
           wc.y < porg.y + 1 || wc.y >= porg.y + N - 1;
}

ivec2 voxelToAtlasPx(ivec3 v) {
    int N   = int(uGridParams.x + 0.5);
    int txN = int(uGridParams.y + 0.5);
    v = clamp(v, ivec3(0), ivec3(N - 1));
    int tx = v.z % txN;
    int ty = v.z / txN;
    return ivec2(tx * N + v.x, ty * N + v.y);
}

vec4 fetch3D(sampler2D tex, ivec3 v) {
    int N = int(uGridParams.x + 0.5);
    ivec3 cl = clamp(v, ivec3(0), ivec3(N - 1));
    if (!isWorldGrid()) {
        return texelFetch(tex, voxelToAtlasPx(cl), 0);
    }
    ivec2 org = worldGridOrigin();
    ivec3 av  = wrapAtlasVoxel(ivec3(org.x + cl.x, cl.y, org.y + cl.z), N);
    return texelFetch(tex, voxelToAtlasPx(av), 0);
}

vec4 sample3DLinear(sampler2D tex, vec3 uvw) {
    float N = uGridParams.x;
    if (isWorldGrid()) {
        int iN = int(N + 0.5);
        vec3 localCell = clamp(uvw, vec3(0.5 / N), vec3(1.0 - 0.5 / N)) * N - 0.5;
        ivec3 l0 = clamp(ivec3(floor(localCell)), ivec3(0), ivec3(iN - 1));
        ivec3 l1 = min(l0 + ivec3(1), ivec3(iN - 1));
        vec3 f = clamp(localCell - vec3(l0), vec3(0.0), vec3(1.0));
        vec4 c000 = fetch3D(tex, ivec3(l0.x, l0.y, l0.z));
        vec4 c100 = fetch3D(tex, ivec3(l1.x, l0.y, l0.z));
        vec4 c010 = fetch3D(tex, ivec3(l0.x, l1.y, l0.z));
        vec4 c110 = fetch3D(tex, ivec3(l1.x, l1.y, l0.z));
        vec4 c001 = fetch3D(tex, ivec3(l0.x, l0.y, l1.z));
        vec4 c101 = fetch3D(tex, ivec3(l1.x, l0.y, l1.z));
        vec4 c011 = fetch3D(tex, ivec3(l0.x, l1.y, l1.z));
        vec4 c111 = fetch3D(tex, ivec3(l1.x, l1.y, l1.z));
        vec4 c00 = mix(c000, c100, f.x);
        vec4 c10 = mix(c010, c110, f.x);
        vec4 c01 = mix(c001, c101, f.x);
        vec4 c11 = mix(c011, c111, f.x);
        return mix(mix(c00, c10, f.y), mix(c01, c11, f.y), f.z);
    }
    float txN = uGridParams.y;
    vec2 invA = 1.0 / gAtlasSize();
    vec3 c    = clamp(uvw, vec3(0.5 / N), vec3(1.0 - 0.5 / N));
    float zf  = c.z * N - 0.5;
    float z0  = clamp(floor(zf), 0.0, N - 1.0);
    float z1  = min(z0 + 1.0, N - 1.0);
    float fz  = zf - z0;

    float tx0 = mod(z0, txN);
    float ty0 = floor(z0 / txN);
    float tx1 = mod(z1, txN);
    float ty1 = floor(z1 / txN);

    vec2 uv0 = (vec2(tx0, ty0) * N + c.xy * N) * invA;
    vec2 uv1 = (vec2(tx1, ty1) * N + c.xy * N) * invA;
    return mix(texture(tex, uv0), texture(tex, uv1), fz);
}

float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 hash33(vec3 p3) {
    p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

float valueNoise3D(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash13(i + vec3(0,0,0)), hash13(i + vec3(1,0,0)), f.x),
            mix(hash13(i + vec3(0,1,0)), hash13(i + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash13(i + vec3(0,0,1)), hash13(i + vec3(1,0,1)), f.x),
            mix(hash13(i + vec3(0,1,1)), hash13(i + vec3(1,1,1)), f.x), f.y),
        f.z);
}

float fbm3D(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += a * valueNoise3D(p);
        p = p * 2.03 + vec3(17.1, 31.7, 11.3);
        a *= 0.5;
    }
    return v;
}

float evalInternalObstacleSdf(vec3 uvw) {
    int kind = int(uEmitterParams2.z + 0.5);
    if (kind == 0) return 10.0;
    vec3 d = uvw - uObstaclePosAndPad.xyz;
    float r = uEmitterParams2.w;
    if (kind == 1) return length(d) - r;
    if (kind == 2) return max(length(d.xz) - r, abs(d.y) - r * 1.6);
    if (kind == 3) {
        vec3 q = abs(d) - vec3(r * 0.95);
        return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
    }
    if (kind == 4) {
        vec3 q = abs(d) - vec3(r * 1.45, r * 0.22, r * 1.45);
        return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
    }
    if (kind == 5) {
        float wall = max(abs(d.x) - r * 1.5, max(abs(d.y) - r * 1.1, abs(d.z) - r * 0.24));
        float hole = length(d.xy) - r * 0.42;
        return max(wall, -hole);
    }
    return 10.0;
}
"""

# ---------------------------------------------------------------------------------------------------------------------
#                                             SHADER DEFINITIONS
# ---------------------------------------------------------------------------------------------------------------------

FULLSCREEN_VERT = r"""#version 450
layout(location = 0) out vec2 vUv;
void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
"""

PYRO_CLEAR_SINGLE_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outFrag0;
void main() {
    outFrag0 = uClearColorVal;
}
"""

PYRO_CLEAR_DUAL_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outFrag0;
layout(location = 1) out vec4 outFrag1;
void main() {
    outFrag0 = uClearColorVal;
    outFrag1 = vec4(0.0);
}
"""

PYRO_SPLAT_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outVel;
layout(location = 1) out vec4 outThermo;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outVel = vec4(0.0);
        outThermo = vec4(0.0);
        return;
    }

    vec4 velData = texelFetch(uTex0, px, 0);
    vec4 thData  = texelFetch(uTex1, px, 0);
    if (worldGridEnteredFresh(v, N)) {
        velData = vec4(0.0);
        thData  = vec4(0.0);
    }
    vec3 uvw     = (vec3(v) + 0.5) / float(N);
    float dt     = uGridParams.w;
    float time   = uTimeAndFlags.x;

    // Boundary & internal/world SDF solid evaluation
    float solid = 0.0;
    vec3  solidVel = vec3(0.0);
    if (v.y <= 0) {
        solid = 1.0;
    }
    if (uDomainBoxMin.w > 0.5 && (v.x <= 0 || v.x >= N - 1 || v.y >= N - 1 || v.z <= 0 || v.z >= N - 1)) {
        solid = 1.0;
    }

    float obsSdf = evalInternalObstacleSdf(uvw);
    if (obsSdf <= 0.0) {
        solid = 1.0;
        solidVel = uObstacleVelAndPad.xyz;
    }

    if (uTimeAndFlags.z > 0.5) {
        vec4 worldSdf = texelFetch(uTex2, voxelToAtlasPx(v), 0);
        float voxelSizeM = max(0.02, (uDomainBoxMax.x - uDomainBoxMin.x) / float(N));
        if (worldSdf.a <= 0.35 * voxelSizeM) {
            solid = 1.0;
            solidVel = worldSdf.rgb;
        } else if (worldSdf.a < 2.2 * voxelSizeM && dot(worldSdf.rgb, worldSdf.rgb) > 1e-4) {
            float wakeFactor = clamp(1.0 - worldSdf.a / (2.2 * voxelSizeM), 0.0, 1.0);
            velData.xyz = mix(velData.xyz, worldSdf.rgb, wakeFactor * clamp(dt * 7.5, 0.0, 0.65));
        }
    }

    if (solid > 0.5) {
        outVel = vec4(solidVel, 1.0);
        outThermo = vec4(0.0, 0.0, 0.0, thData.a * 0.5);
        return;
    }

    vec3 boxSpan     = max(vec3(0.25), uDomainBoxMax.xyz - uDomainBoxMin.xyz);
    vec3 metricScale = max(vec3(0.75), pow(boxSpan, vec3(0.55)));

    // Continuous nozzle emitter (1:1 port of shaders-glsl.js SPLAT_FRAG, with SPIR-V edge0 < edge1 smoothstep!)
    if (uEmitterParams0.x > 0.5 && uEmitterParams0.y > 0.001) {
        vec3 emCenter = vec3(0.5, uEmitterParams0.w / metricScale.y, 0.5);
        vec3 diff = (uvw - emCenter) * metricScale;
        float r = max(0.025, uEmitterParams0.z);
        float d = length(vec3(diff.x, diff.y * 1.45, diff.z));
        if (d < r * 1.35) {
            float falloff = 1.0 - smoothstep(r * 0.15, r * 1.35, d);
            vec3 np = uvw * boxSpan * 8.5 + vec3(0.0, -time * 5.5, time * 1.7);
            float n = 0.55 + 0.45 * valueNoise3D(np);
            float rate = falloff * uEmitterParams0.y * n;

            thData.r = max(thData.r, uEmitterParams1.x * falloff * (0.6 + 0.4 * n));
            thData.g = max(thData.g, uEmitterParams1.y * rate);
            thData.b = max(thData.b, uEmitterParams1.z * rate);
            thData.a = max(thData.a, uEmitterParams1.z * rate * 0.9);

            vec3 tangent = vec3(-diff.z, 0.0, diff.x) / max(length(diff.xz), 0.02);
            vec3 turb = (hash33(np) - 0.5) * 0.9;
            vec3 injVel = vec3(0.0, uEmitterParams1.w * (0.75 + 0.5 * n), 0.0)
                        + tangent * uEmitterParams2.x * (d / r)
                        + turb;
            velData.xyz = mix(velData.xyz, injVel, clamp(falloff * dt * 14.0, 0.0, 0.85));
        }
    }

    // 3D interactive flamethrower brush
    if (uEmitterParams2.y > 0.5) {
        vec3 bDiff = (uvw - uBrushPosAndPad.xyz) * metricScale;
        float bd = length(bDiff);
        float br = 0.14;
        if (bd < br) {
            float bf = 1.0 - smoothstep(br * 0.15, br, bd);
            vec3 np = uvw * boxSpan * 10.0 + vec3(time * 4.0);
            float n = 0.6 + 0.4 * valueNoise3D(np);
            velData.xyz += (uBrushVelAndPad.xyz + vec3(0.0, 2.5, 0.0)) * bf * 0.65;
            thData.r = max(thData.r, 2.2 * bf * n);
            thData.g = max(thData.g, 4.8 * bf * n);
            thData.b = max(thData.b, 3.8 * bf * n);
            thData.a = max(thData.a, 4.2 * bf);
        }
    }

    // Multi-lobe shockwave blast impulse (1:1 port of shaders-glsl.js SPLAT_FRAG)
    if (uBlastThermoParams.x > 0.5) {
        vec3 bDiff = (uvw - uBlastCenterAndRadius.xyz) * metricScale;
        float bDist = length(bDiff);
        float bRad  = max(0.04, uBlastCenterAndRadius.w);
        vec3 bDir   = bDist > 1e-4 ? (bDiff / max(bDist, 1e-4)) : vec3(0.0, 1.0, 0.0);
        vec3 lobeCoord = bDir * max(1.0, uBlastExtraParams.y) + vec3(uBlastExtraParams.z * 7.13, uBlastExtraParams.z * 3.71, uBlastExtraParams.z * 5.39);
        float lobeN = valueNoise3D(lobeCoord) * 0.35 + valueNoise3D(lobeCoord * 2.1) * 0.18;
        float effR  = bRad * (1.0 + lobeN);
        if (bDist < effR * 1.25) {
            float coreMask  = 1.0 - smoothstep(effR * 0.10, effR, bDist);
            float shellMask = (1.0 - smoothstep(effR * 0.45, effR * 1.20, bDist)) * smoothstep(0.0, effR * 0.50, bDist);
            float velMask   = 1.0 - smoothstep(effR * 0.12, effR * 1.25, bDist);
            velData.xyz += (bDir + vec3(0.0, 0.38, 0.0)) * uBlastThermoParams.y * velMask + uBlastDirVelAndPad.xyz * coreMask;
            thData.r = min(14.0, thData.r + uBlastExtraParams.x * (shellMask * 0.9 + coreMask * 0.45));
            thData.g = max(thData.g, uBlastThermoParams.z * coreMask * (1.0 + 0.3 * lobeN));
            thData.b = max(thData.b, uBlastThermoParams.w * coreMask);
            thData.a = max(thData.a, uBlastThermoParams.z * coreMask);
        }
    }

    // Ballistic shrapnel trails
    int projCount = int(uBlastExtraParams.w + 0.5);
    for (int i = 0; i < 6; ++i) {
        if (i >= projCount) break;
        vec3 pDiff = (uvw - uProjData[i].xyz) * metricScale;
        float pDist = length(pDiff);
        float pRad  = max(0.025, uProjData[i].w);
        if (pDist < pRad) {
            float pk = 1.0 - smoothstep(0.0, pRad, pDist);
            velData.xyz = mix(velData.xyz, uProjVel[i].xyz * 0.55, pk * 0.7);
            thData.r = min(12.0, thData.r + 2.4 * pk * dt * 14.0);
            thData.g = max(thData.g, 3.6 * pk);
            thData.b = max(thData.b, 2.2 * pk);
        }
    }

    // External 4-wheel tyre contact-patch, wheel-arch & burning-sidewall sources
    int extCount = int(uBlastDirVelAndPad.w + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= extCount) break;
        if (isWorldGrid()) {
            vec3 eDiffM = (uvw - uExtPosRad[i].xyz) * boxSpan;
            float cellM = boxSpan.x / float(N);
            float eRadM = max(1.35 * cellM, uExtPosRad[i].w * boxSpan.x);
            float eDistM = length(eDiffM);
            if (eDistM < eRadM * 1.40) {
                vec3 worldP = uDomainBoxMin.xyz + uvw * boxSpan;
                float n = 0.68 + 0.64 * valueNoise3D(worldP * 1.85 + vec3(time * 5.2, float(i) * 3.7, -time * 3.9));
                float ek = (1.0 - smoothstep(eRadM * 0.12, eRadM * 1.40, eDistM)) * n;
                float rate = uExtVelRate[i].w * dt * ek;
                thData.r = min(14.0, thData.r + uExtThermo[i].x * rate * 1.55);
                thData.g = max(thData.g, uExtThermo[i].y * ek);
                thData.b = min(14.0, thData.b + uExtThermo[i].z * rate * 1.35);
                velData.xyz = mix(velData.xyz, uExtVelRate[i].xyz, clamp(rate * 0.65, 0.0, 0.78));
            }
        } else {
            vec3 eDiff = uvw - uExtPosRad[i].xyz;
            float eDist = length(eDiff);
            float eRad  = max(0.022, uExtPosRad[i].w);
            if (eDist < eRad * 1.35) {
                float n = 0.68 + 0.64 * valueNoise3D(uvw * 14.0 + vec3(time * 5.2, float(i) * 3.7, -time * 3.9));
                float ek = (1.0 - smoothstep(eRad * 0.12, eRad * 1.35, eDist)) * n;
                float rate = uExtVelRate[i].w * dt * ek;
                thData.r = min(12.0, thData.r + uExtThermo[i].x * rate * 1.35);
                thData.g = max(thData.g, uExtThermo[i].y * ek);
                thData.b = min(12.0, thData.b + uExtThermo[i].z * rate * 1.25);
                velData.xyz = mix(velData.xyz, uExtVelRate[i].xyz, clamp(rate * 0.65, 0.0, 0.78));
            }
        }
    }

    velData.a = 0.0;
    outVel = velData;
    outThermo = thData;
}
"""

PYRO_ADVECT_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outVel;
layout(location = 1) out vec4 outThermo;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outVel = vec4(0.0);
        outThermo = vec4(0.0);
        return;
    }

    vec4 selfVel = texelFetch(uTex0, px, 0);
    if (selfVel.a > 0.5) {
        outVel = selfVel;
        outThermo = vec4(0.0);
        return;
    }

    vec3 boxSpan  = max(vec3(0.25), uDomainBoxMax.xyz - uDomainBoxMin.xyz);
    vec3 uvw      = (vec3(v) + 0.5) / float(N);
    float dt      = uGridParams.w;
    vec3 velScale = isWorldGrid()
        ? (vec3(dt) / boxSpan)
        : ((dt * 0.34) / max(vec3(0.75), pow(boxSpan, vec3(0.65))));

    // RK2 midpoint backtrace in normalised UVW
    vec3 uvwMid  = clamp(uvw - 0.5 * selfVel.xyz * velScale, vec3(0.5 / float(N)), vec3(1.0 - 0.5 / float(N)));
    vec3 velMid  = sample3DLinear(uTex0, uvwMid).xyz;
    vec3 uvwBack = clamp(uvw - velMid * velScale, vec3(0.5 / float(N)), vec3(1.0 - 0.5 / float(N)));

    vec4 advVel = sample3DLinear(uTex0, uvwBack);
    vec4 advTh  = sample3DLinear(uTex1, uvwBack);
    if (advVel.a > 0.5) {
        advVel.xyz = selfVel.xyz * 0.5;
        advTh = texelFetch(uTex1, px, 0) * 0.92;
    }

    float sponge = 1.0;
    if (uDomainBoxMin.w < 0.5) {
        float edgeX = min(uvw.x, 1.0 - uvw.x);
        float edgeZ = min(uvw.z, 1.0 - uvw.z);
        float topY  = 1.0 - uvw.y;
        sponge = smoothstep(0.0, 0.048, min(edgeX, edgeZ)) * smoothstep(0.0, 0.045, topY);
    }

    outVel    = vec4(advVel.xyz * uAdvectAndForces0.w, 0.0);
    outThermo = max(vec4(0.0), vec4(
        advTh.r * uAdvectAndForces0.x * sponge,
        advTh.g * uAdvectAndForces0.y * sponge,
        advTh.b * uAdvectAndForces0.z * sponge,
        advTh.a * 0.88 * sponge));
}
"""

PYRO_MACCORMACK_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outVel;
layout(location = 1) out vec4 outThermo;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outVel = vec4(0.0);
        outThermo = vec4(0.0);
        return;
    }

    // uTex0 = velPrev, uTex1 = velHat (forward advected), uTex2 = thermoPrev, uTex3 = thermoHat
    vec4 velPrev = texelFetch(uTex0, px, 0);
    if (velPrev.a > 0.5) {
        outVel = velPrev;
        outThermo = vec4(0.0);
        return;
    }

    vec4 velHat   = texelFetch(uTex1, px, 0);
    vec4 thHat    = texelFetch(uTex3, px, 0);
    vec3 boxSpan  = max(vec3(0.25), uDomainBoxMax.xyz - uDomainBoxMin.xyz);
    vec3 uvw      = (vec3(v) + 0.5) / float(N);
    float dt      = uGridParams.w;
    vec3 velScale = isWorldGrid()
        ? (vec3(dt) / boxSpan)
        : ((dt * 0.34) / max(vec3(0.75), pow(boxSpan, vec3(0.65))));

    // Reverse step (+dt) from forward-advected state
    vec3 uvwFwd  = clamp(uvw + velPrev.xyz * velScale, vec3(0.5 / float(N)), vec3(1.0 - 0.5 / float(N)));
    vec4 velRev  = sample3DLinear(uTex1, uvwFwd);
    vec4 thRev   = sample3DLinear(uTex3, uvwFwd);

    vec3 uvwBack = clamp(uvw - velPrev.xyz * velScale, vec3(0.5 / float(N)), vec3(1.0 - 0.5 / float(N)));
    ivec3 b0 = clamp(ivec3(floor(uvwBack * float(N) - 0.5)), ivec3(0), ivec3(N - 1));
    ivec3 b1 = min(b0 + ivec3(1), ivec3(N - 1));

    // Neighborhood min/max limiter prevents any BFECC ringing or negative overshoots
    vec3 vMin = vec3( 1e6), vMax = vec3(-1e6);
    vec4 tMin = vec4( 1e6), tMax = vec4(-1e6);
    for (int dz = 0; dz <= 1; ++dz)
    for (int dy = 0; dy <= 1; ++dy)
    for (int dx = 0; dx <= 1; ++dx) {
        ivec3 c = ivec3(dx == 0 ? b0.x : b1.x, dy == 0 ? b0.y : b1.y, dz == 0 ? b0.z : b1.z);
        vec4 sv = fetch3D(uTex0, c);
        vec4 st = fetch3D(uTex2, c);
        vMin = min(vMin, sv.xyz); vMax = max(vMax, sv.xyz);
        tMin = min(tMin, st);     tMax = max(tMax, st);
    }

    vec3 corrVel = clamp(velHat.xyz + 0.45 * (velPrev.xyz - velRev.xyz), vMin, vMax);
    vec4 corrTh  = clamp(thHat + 0.45 * (texelFetch(uTex2, px, 0) - thRev), max(vec4(0.0), tMin), tMax);

    float sponge = 1.0;
    if (uDomainBoxMin.w < 0.5) {
        float edgeX = min(uvw.x, 1.0 - uvw.x);
        float edgeZ = min(uvw.z, 1.0 - uvw.z);
        float topY  = 1.0 - uvw.y;
        sponge = smoothstep(0.0, 0.048, min(edgeX, edgeZ)) * smoothstep(0.0, 0.045, topY);
    }

    outVel    = vec4(corrVel * uAdvectAndForces0.w, 0.0);
    outThermo = vec4(
        corrTh.r * uAdvectAndForces0.x * sponge,
        corrTh.g * uAdvectAndForces0.y * sponge,
        corrTh.b * uAdvectAndForces0.z * sponge,
        corrTh.a * 0.90 * sponge);
}
"""

PYRO_CURL_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outCurl;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outCurl = vec4(0.0);
        return;
    }

    vec3 vL = fetch3D(uTex0, v - ivec3(1, 0, 0)).xyz;
    vec3 vR = fetch3D(uTex0, v + ivec3(1, 0, 0)).xyz;
    vec3 vD = fetch3D(uTex0, v - ivec3(0, 1, 0)).xyz;
    vec3 vU = fetch3D(uTex0, v + ivec3(0, 1, 0)).xyz;
    vec3 vB = fetch3D(uTex0, v - ivec3(0, 0, 1)).xyz;
    vec3 vF = fetch3D(uTex0, v + ivec3(0, 0, 1)).xyz;

    vec3 curl = 0.5 * vec3(
        (vU.z - vD.z) - (vF.y - vB.y),
        (vF.x - vB.x) - (vR.z - vL.z),
        (vR.y - vL.y) - (vU.x - vD.x));
    outCurl = vec4(curl, length(curl));
}
"""

PYRO_VORTICITY_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outVel;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outVel = vec4(0.0);
        return;
    }

    vec4 vel = texelFetch(uTex0, px, 0);
    if (vel.a > 0.5) {
        outVel = vel;
        return;
    }

    float dt = uGridParams.w;
    vec3 uvw = (vec3(v) + 0.5) / float(N);

    if (uTimeAndFlags.y > 0.5 && uAdvectAndForces1.x > 0.001) {
        float cL = fetch3D(uTex1, v - ivec3(1, 0, 0)).w;
        float cR = fetch3D(uTex1, v + ivec3(1, 0, 0)).w;
        float cD = fetch3D(uTex1, v - ivec3(0, 1, 0)).w;
        float cU = fetch3D(uTex1, v + ivec3(0, 1, 0)).w;
        float cB = fetch3D(uTex1, v - ivec3(0, 0, 1)).w;
        float cF = fetch3D(uTex1, v + ivec3(0, 0, 1)).w;
        vec3 eta = 0.5 * vec3(cR - cL, cU - cD, cF - cB);
        float lenEta = length(eta);
        if (lenEta > 1e-5) {
            vec3 Ndir = eta / lenEta;
            vec3 omega = texelFetch(uTex1, px, 0).xyz;
            vel.xyz += dt * uAdvectAndForces1.x * cross(Ndir, omega);
        }
    }

    vec4 th = texelFetch(uTex2, px, 0);
    float activity = clamp(th.r * 0.60 + th.g * 0.80 + th.b * 0.50, 0.0, 1.5);
    if (uAdvectAndForces1.y > 0.001 && activity > 0.01) {
        vec3 baseCoord = isWorldGrid()
            ? ((uDomainBoxMin.xyz + uvw * (uDomainBoxMax.xyz - uDomainBoxMin.xyz)) * 0.22)
            : uvw;
        vec3 np = baseCoord * max(0.5, uAdvectAndForces1.z) + vec3(uTimeAndFlags.x * 0.65, -uTimeAndFlags.x * 1.35, uTimeAndFlags.x * 0.45);
        vec3 turb = vec3(
            fbm3D(np + vec3(13.1, 7.7, 3.3)) - 0.5,
            fbm3D(np + vec3(5.9, 21.3, 17.4)) - 0.5,
            fbm3D(np + vec3(29.2, 2.5, 41.8)) - 0.5);
        vel.xyz += turb * (uAdvectAndForces1.y * activity * dt * 2.4);
    }

    float windHeightMask = smoothstep(0.05, 0.65, uvw.y);
    vel.xz += vec2(uAdvectAndForces1.w, uAdvectAndForces2.x) * activity * windHeightMask * dt;
    outVel = vel;
}
"""

PYRO_BUOYANCY_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outVel;
layout(location = 1) out vec4 outThermo;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outVel = vec4(0.0);
        outThermo = vec4(0.0);
        return;
    }

    vec4 vel = texelFetch(uTex0, px, 0);
    if (vel.a > 0.5) {
        outVel = vel;
        outThermo = vec4(0.0);
        return;
    }

    vec4 th  = texelFetch(uTex1, px, 0);
    float dt = uGridParams.w;

    float smoke = clamp(th.r, 0.0, 16.0);
    float temp  = clamp(th.g, 0.0, 16.0);
    float fuel  = clamp(th.b, 0.0, 16.0);

    // Arrhenius-like fuel combustion -> heat + soot + divergence expansion (1:1 shaders-glsl.js)
    float ignition = smoothstep(0.08, 0.65, temp + fuel * 0.50);
    float burned   = min(fuel, fuel * uAdvectAndForces2.y * (0.65 + 0.65 * ignition) * dt);
    fuel  = clamp(fuel - burned, 0.0, 16.0);
    temp  = clamp(temp + burned * uAdvectAndForces2.z, 0.0, 14.0);
    smoke = clamp(smoke + burned * uAdvectAndForces2.w, 0.0, 16.0);

    // Stefan-Boltzmann T^2 radiative cooling + convective cooling (strictly FP16-safe!)
    float coolRate = -log(clamp(uAdvectAndForces0.y, 0.01, 0.99999)) / max(dt * 0.35, 1e-4);
    temp = max(0.0, temp - coolRate * (0.55 * temp + 0.14 * temp * temp) * dt);

    // Thermal buoyancy lift minus cold particulate drag + baroclinic mushroom-cap torque
    float tL = fetch3D(uTex1, v - ivec3(1, 0, 0)).g;
    float tR = fetch3D(uTex1, v + ivec3(1, 0, 0)).g;
    float tB = fetch3D(uTex1, v - ivec3(0, 0, 1)).g;
    float tF = fetch3D(uTex1, v + ivec3(0, 0, 1)).g;
    vec3 baroclinic = vec3(tR - tL, 0.0, tF - tB) * (0.35 * uAdvectAndForces3.x);
    float lift = uAdvectAndForces3.x * temp - uAdvectAndForces3.y * smoke * 0.45;
    vel.xyz += (vec3(0.0, lift, 0.0) + baroclinic) * dt;

    float speed = length(vel.xyz);
    if (speed > 20.0) {
        vel.xyz = (vel.xyz / speed) * 20.0;
    }

    float reactionRate = clamp(burned / max(dt, 1e-4), 0.0, 16.0);
    outVel    = vec4(vel.xyz, 0.0);
    outThermo = vec4(smoke, temp, fuel, reactionRate);
}
"""

PYRO_DIVERGENCE_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outDiv;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outDiv = vec4(0.0);
        return;
    }

    vec4 self = texelFetch(uTex0, px, 0);
    if (self.a > 0.5) {
        outDiv = vec4(0.0);
        return;
    }

    vec4 vL = fetch3D(uTex0, v - ivec3(1, 0, 0));
    vec4 vR = fetch3D(uTex0, v + ivec3(1, 0, 0));
    vec4 vD = fetch3D(uTex0, v - ivec3(0, 1, 0));
    vec4 vU = fetch3D(uTex0, v + ivec3(0, 1, 0));
    vec4 vB = fetch3D(uTex0, v - ivec3(0, 0, 1));
    vec4 vF = fetch3D(uTex0, v + ivec3(0, 0, 1));

    // Solid voxels store solidVel in .xyz (0 for static, vehicle wake for moving car); clamp y=0 floor
    float xL = vL.x;
    float xR = vR.x;
    float yD = (v.y <= 0) ? 0.0 : vD.y;
    float yU = vU.y;
    float zB = vB.z;
    float zF = vF.z;

    float div = 0.5 * ((xR - xL) + (yU - yD) + (zF - zB));

    vec4 th = texelFetch(uTex1, px, 0);
    float expansion = th.a * uAdvectAndForces3.z * 0.065;
    float fireSrc   = clamp(th.g * 0.45 + th.b * 0.35, 0.0, 6.0);

    outDiv = vec4(div - expansion, fireSrc, 0.0, 0.0);
}
"""

PYRO_JACOBI_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outPres;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outPres = vec4(0.0);
        return;
    }

    vec4 selfVel = texelFetch(uTex2, px, 0);
    if (selfVel.a > 0.5 || worldGridEnteredFresh(v, N)) {
        outPres = vec4(0.0);
        return;
    }

    vec2 centerP = texelFetch(uTex0, px, 0).rg;
    vec2 divSrc  = texelFetch(uTex1, px, 0).rg;

    // Pure Neumann boundary across solid neighbours & domain faces (1:1 shaders-glsl.js PRESSURE_JACOBI_FRAG)
    vec2 pL = fetch3D(uTex2, v - ivec3(1, 0, 0)).a > 0.5 ? centerP : fetch3D(uTex0, v - ivec3(1, 0, 0)).rg;
    vec2 pR = fetch3D(uTex2, v + ivec3(1, 0, 0)).a > 0.5 ? centerP : fetch3D(uTex0, v + ivec3(1, 0, 0)).rg;
    vec2 pD = (fetch3D(uTex2, v - ivec3(0, 1, 0)).a > 0.5 || v.y <= 0) ? centerP : fetch3D(uTex0, v - ivec3(0, 1, 0)).rg;
    vec2 pU = fetch3D(uTex2, v + ivec3(0, 1, 0)).a > 0.5 ? centerP : fetch3D(uTex0, v + ivec3(0, 1, 0)).rg;
    vec2 pB = fetch3D(uTex2, v - ivec3(0, 0, 1)).a > 0.5 ? centerP : fetch3D(uTex0, v - ivec3(0, 0, 1)).rg;
    vec2 pF = fetch3D(uTex2, v + ivec3(0, 0, 1)).a > 0.5 ? centerP : fetch3D(uTex0, v + ivec3(0, 0, 1)).rg;

    vec2 sumN = pL + pR + pD + pU + pB + pF;
    float nextPressure   = ((sumN.x - divSrc.x) / 6.0) * 0.985;
    float nextIrradiance = (sumN.y + divSrc.y * 0.55) / 6.28;
    outPres = vec4(nextPressure, nextIrradiance, 0.0, 0.0);
}
"""

PYRO_GRADIENT_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) out vec4 outVel;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    int N = int(gRes() + 0.5);
    ivec3 v = atlasPxToVoxel(px);
    if (v.z >= N) {
        outVel = vec4(0.0);
        return;
    }

    vec4 vel = texelFetch(uTex0, px, 0);
    if (vel.a > 0.5) {
        outVel = vel;
        return;
    }

    float pC = texelFetch(uTex1, px, 0).r;
    vec4 sL = fetch3D(uTex0, v - ivec3(1, 0, 0));
    vec4 sR = fetch3D(uTex0, v + ivec3(1, 0, 0));
    vec4 sD = fetch3D(uTex0, v - ivec3(0, 1, 0));
    vec4 sU = fetch3D(uTex0, v + ivec3(0, 1, 0));
    vec4 sB = fetch3D(uTex0, v - ivec3(0, 0, 1));
    vec4 sF = fetch3D(uTex0, v + ivec3(0, 0, 1));

    float pL = sL.a > 0.5 ? pC : fetch3D(uTex1, v - ivec3(1, 0, 0)).r;
    float pR = sR.a > 0.5 ? pC : fetch3D(uTex1, v + ivec3(1, 0, 0)).r;
    float pD = sD.a > 0.5 ? pC : fetch3D(uTex1, v - ivec3(0, 1, 0)).r;
    float pU = sU.a > 0.5 ? pC : fetch3D(uTex1, v + ivec3(0, 1, 0)).r;
    float pB = sB.a > 0.5 ? pC : fetch3D(uTex1, v - ivec3(0, 0, 1)).r;
    float pF = sF.a > 0.5 ? pC : fetch3D(uTex1, v + ivec3(0, 0, 1)).r;

    vel.xyz -= 0.5 * vec3(pR - pL, pU - pD, pF - pB);

    // Free-slip solid wall projection
    if (sL.a > 0.5 && vel.x < sL.x) vel.x = sL.x;
    if (sR.a > 0.5 && vel.x > sR.x) vel.x = sR.x;
    if (sD.a > 0.5 && vel.y < sD.y) vel.y = sD.y;
    if (sU.a > 0.5 && vel.y > sU.y) vel.y = sU.y;
    if (sB.a > 0.5 && vel.z < sB.z) vel.z = sB.z;
    if (sF.a > 0.5 && vel.z > sF.z) vel.z = sF.z;

    outVel = vec4(vel.xyz, 0.0);
}
"""

PYRO_RAYMARCH_FRAG = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 blackbodyPalette(float tempRaw, float tempScale, int palette) {
    float t = clamp(tempRaw * tempScale * 0.28, 0.0, 1.6);
    if (t <= 0.01) return vec3(0.0);

    if (palette == 0) {
        vec3 c0 = vec3(0.0);
        vec3 c1 = vec3(0.55, 0.03, 0.005);
        vec3 c2 = vec3(1.00, 0.24, 0.01);
        vec3 c3 = vec3(1.00, 0.68, 0.10);
        vec3 c4 = vec3(1.00, 0.96, 0.78);
        vec3 col = mix(c0, c1, smoothstep(0.0, 0.22, t));
        col = mix(col, c2, smoothstep(0.18, 0.52, t));
        col = mix(col, c3, smoothstep(0.48, 0.88, t));
        col = mix(col, c4, smoothstep(0.82, 1.35, t));
        return col * (t * t * 1.45);
    } else if (palette == 1) {
        vec3 col = vec3(
            smoothstep(0.02, 0.32, t),
            pow(max(clamp((t - 0.18) / 0.85, 0.0, 1.2), 1e-5), 1.75) * step(0.18, t),
            pow(max(clamp((t - 0.58) / 0.75, 0.0, 1.2), 1e-5), 2.40) * step(0.58, t));
        return col * (t * t * 1.6);
    } else if (palette == 2) {
        vec3 col = vec3(
            smoothstep(0.01, 0.22, t),
            smoothstep(0.10, 0.55, t),
            smoothstep(0.28, 0.95, t) * 1.15);
        return col * (t * t * 1.85);
    } else if (palette == 3) {
        vec3 c1 = vec3(0.02, 0.38, 0.08);
        vec3 c2 = vec3(0.14, 0.95, 0.28);
        vec3 c3 = vec3(0.72, 1.00, 0.45);
        vec3 c4 = vec3(0.92, 1.00, 0.90);
        vec3 col = mix(vec3(0.0), c1, smoothstep(0.0, 0.25, t));
        col = mix(col, c2, smoothstep(0.22, 0.58, t));
        col = mix(col, c3, smoothstep(0.55, 0.92, t));
        col = mix(col, c4, smoothstep(0.88, 1.35, t));
        return col * (t * t * 1.55);
    } else if (palette == 4) {
        vec3 c1 = vec3(0.04, 0.12, 0.65);
        vec3 c2 = vec3(0.08, 0.58, 1.00);
        vec3 c3 = vec3(0.45, 0.92, 1.00);
        vec3 c4 = vec3(0.95, 0.99, 1.00);
        vec3 col = mix(vec3(0.0), c1, smoothstep(0.0, 0.25, t));
        col = mix(col, c2, smoothstep(0.22, 0.58, t));
        col = mix(col, c3, smoothstep(0.55, 0.92, t));
        col = mix(col, c4, smoothstep(0.88, 1.35, t));
        return col * (t * t * 1.65);
    }
    vec3 c1 = vec3(0.32, 0.02, 0.48);
    vec3 c2 = vec3(0.88, 0.08, 0.65);
    vec3 c3 = vec3(1.00, 0.48, 0.88);
    vec3 c4 = vec3(1.00, 0.92, 1.00);
    vec3 col = mix(vec3(0.0), c1, smoothstep(0.0, 0.25, t));
    col = mix(col, c2, smoothstep(0.22, 0.58, t));
    col = mix(col, c3, smoothstep(0.55, 0.92, t));
    col = mix(col, c4, smoothstep(0.88, 1.35, t));
    return col * (t * t * 1.60);
}

float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(0.02, 1.0 + g2 - 2.0 * g * cosTheta), 1.5));
}

vec2 intersectBox(vec3 ro, vec3 rd, vec3 bMin, vec3 bMax) {
    vec3 safeRd = mix(rd, vec3(1e-6), lessThan(abs(rd), vec3(1e-6)));
    vec3 invD = 1.0 / safeRd;
    vec3 t0 = (bMin - ro) * invD;
    vec3 t1 = (bMax - ro) * invD;
    vec3 tSmaller = min(t0, t1);
    vec3 tBigger  = max(t0, t1);
    float tNear = max(max(tSmaller.x, tSmaller.y), tSmaller.z);
    float tFar  = min(min(tBigger.x, tBigger.y), tBigger.z);
    return vec2(tNear, tFar);
}

vec3 acesTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // PiP 2D-tiled 3D volume atlas minimap overlay (bottom-right corner in standalone FluidEditor)
    if (uCamForwardAndComp.w < 0.5 && uSliceAndPipParams.z > 0.5) {
        vec2 pipMin = vec2(0.76, 0.68);
        vec2 pipMax = vec2(0.985, 0.965);
        if (vUv.x >= pipMin.x && vUv.x <= pipMax.x && vUv.y >= pipMin.y && vUv.y <= pipMax.y) {
            vec2 aUv = (vUv - pipMin) / (pipMax - pipMin);
            int field = int(uSliceAndPipParams.w + 0.5);
            vec3 col = vec3(0.05);
            if (field == 0) {
                vec4 th = texture(uTex0, aUv);
                col = vec3(th.r * 0.25) + blackbodyPalette(th.g, 1.0, 0);
            } else if (field == 1) {
                col = abs(texture(uTex1, aUv).xyz) * 0.35;
            } else if (field == 2) {
                col = abs(texture(uTex3, aUv).xyz) * 0.28;
            } else {
                vec2 pi = texture(uTex2, aUv).rg;
                col = vec3(abs(pi.r) * 2.0, pi.g * 1.4, abs(pi.r));
            }
            outColor = vec4(acesTonemap(col), 1.0);
            return;
        }
    }

    // Vulkan top-down UV: vUv=(0,0) is top-left -> ndc=(-1, +1) (+uCamUp)
    vec2 ndc = vec2(vUv.x * 2.0 - 1.0, 1.0 - vUv.y * 2.0);
    float tanHalf = uCamPosAndFov.w;
    float aspect  = uDomainBoxMax.w;
    vec3 ro = uCamPosAndFov.xyz;
    vec3 rd = normalize(
        uCamForwardAndComp.xyz +
        ndc.x * aspect * tanHalf * uCamRightAndTransp.xyz +
        ndc.y * tanHalf * uCamUpAndChannel.xyz);

    vec3 bMin = uDomainBoxMin.xyz;
    vec3 bMax = uDomainBoxMax.xyz;
    vec3 bSpan = max(vec3(0.05), bMax - bMin);

    bool compositeWorld = (uCamForwardAndComp.w > 0.5);
    bool transparentBg  = (uCamRightAndTransp.w > 0.5);
    int  channel        = int(uCamUpAndChannel.w + 0.5);

    // Background dark studio sky & lit studio floor (1:1 port of shaders-glsl.js RAYMARCH_FRAG)
    vec3 sunDir = normalize(uSunDirAndIntensity.xyz);
    float skyGrad = clamp(rd.y * 0.5 + 0.5, 1e-5, 1.0);
    vec3 bgCol = mix(vec3(0.035, 0.040, 0.052), vec3(0.012, 0.015, 0.022), pow(skyGrad, 0.7));
    float sunGlare = pow(max(dot(rd, sunDir), 1e-5), 24.0) * 0.09;
    bgCol += vec3(1.0, 0.78, 0.55) * sunGlare;

    float tSceneClip = 1e6;
    if (compositeWorld) {
        tSceneClip = texture(uTex4, vUv).r;
    } else if (!transparentBg && rd.y < -1e-4) {
        float tFloor = (-0.6 - ro.y) / rd.y;
        if (tFloor > 0.0 && tFloor < 60.0) {
            tSceneClip = tFloor;
            vec3 fp = ro + rd * tFloor;
            vec2 grid = abs(fract(fp.xz * 2.0 - 0.5) - 0.5) / fwidth(fp.xz * 2.0);
            float line = 1.0 - min(min(grid.x, grid.y), 1.0);
            float fade = exp(-0.08 * length(fp.xz));
            bgCol = mix(vec3(0.042, 0.044, 0.048), vec3(0.095, 0.10, 0.11), line * fade * 0.55);
        }
    }

    // Internal analytical obstacle hit in standalone FluidEditor
    if (!compositeWorld && int(uEmitterParams2.z + 0.5) > 0) {
        vec2 tb = intersectBox(ro, rd, bMin, bMax);
        if (tb.y > max(tb.x, 0.0)) {
            float tm = max(tb.x, 0.0);
            float tEnd = min(tb.y, tSceneClip);
            for (int s = 0; s < 48; ++s) {
                if (tm > tEnd) break;
                vec3 pUvw = (ro + rd * tm - bMin) / bSpan;
                float d = evalInternalObstacleSdf(pUvw) * bSpan.x;
                if (d < 0.006) {
                    tSceneClip = tm;
                    vec2 eps = vec2(0.006, 0.0);
                    vec3 n = normalize(vec3(
                        evalInternalObstacleSdf(pUvw + eps.xyy) - evalInternalObstacleSdf(pUvw - eps.xyy),
                        evalInternalObstacleSdf(pUvw + eps.yxy) - evalInternalObstacleSdf(pUvw - eps.yxy),
                        evalInternalObstacleSdf(pUvw + eps.yyx) - evalInternalObstacleSdf(pUvw - eps.yyx)));
                    float ndl = max(dot(n, sunDir), 0.0);
                    bgCol = vec3(0.22, 0.23, 0.26) * (0.22 + 0.78 * ndl);
                    break;
                }
                tm += max(0.015, d * 0.75);
            }
        }
    }

    // Cross-section debug slice mode (channel == 8)
    if (!compositeWorld && channel == 8) {
        int axis = int(uSliceAndPipParams.x + 0.5);
        float sPos = uSliceAndPipParams.y;
        float planeCoord = (axis == 0) ? mix(bMin.x, bMax.x, sPos)
                         : (axis == 1) ? mix(bMin.y, bMax.y, sPos)
                                       : mix(bMin.z, bMax.z, sPos);
        float roC = (axis == 0) ? ro.x : (axis == 1) ? ro.y : ro.z;
        float rdC = (axis == 0) ? rd.x : (axis == 1) ? rd.y : rd.z;
        if (abs(rdC) > 1e-5) {
            float tSlice = (planeCoord - roC) / rdC;
            if (tSlice > 0.0 && tSlice < tSceneClip) {
                vec3 hp = ro + rd * tSlice;
                if (all(greaterThanEqual(hp, bMin)) && all(lessThanEqual(hp, bMax))) {
                    vec3 uvw = (hp - bMin) / bSpan;
                    vec4 th = sample3DLinear(uTex0, uvw);
                    vec3 heat = blackbodyPalette(th.g, uOpticsParams1.x, int(uOpticsParams1.y + 0.5));
                    vec3 col = vec3(th.r * 0.25) + heat + vec3(0.0, 0.08, 0.16);
                    outColor = vec4(acesTonemap(col * uOpticsParams2.y), 1.0);
                    return;
                }
            }
        }
    }

    vec2 tBox = intersectBox(ro, rd, bMin, bMax);
    float tStart = max(tBox.x, 0.0);
    float tStop  = min(tBox.y, tSceneClip);

    if (tStop <= tStart) {
        if (compositeWorld || transparentBg) {
            outColor = vec4(0.0);
        } else {
            outColor = vec4(acesTonemap(bgCol * uOpticsParams2.y), 1.0);
        }
        return;
    }

    int steps = clamp(int(uOpticsParams3.y + 0.5), 24, 192);
    float boxDiag      = length(bSpan);
    float stepLen      = isWorldGrid()
        ? max(0.08, (tStop - tStart) / float(steps))
        : ((boxDiag * 0.82) / float(steps));
    float opticalScale = isWorldGrid()
        ? 0.42
        : (1.75 / max(1.0, boxDiag));
    float jitter       = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float t            = tStart + jitter * stepLen;

    float transmittance = 1.0;
    vec3  radiance      = vec3(0.0);
    float cosTheta      = dot(rd, sunDir);
    float phase         = mix(henyeyGreenstein(cosTheta, -0.25),
                              henyeyGreenstein(cosTheta, uOpticsParams2.x), 0.72);

    int shadowSteps = clamp(int(uOpticsParams3.z + 0.5), 2, 24);
    int palette     = int(uOpticsParams1.y + 0.5);
    int quantRes    = int(uOpticsParams3.w + 0.5);

    for (int i = 0; i < 192; ++i) {
        if (i >= steps || t >= tStop || transmittance < 0.008) break;
        vec3 pos = ro + rd * t;
        vec3 uvw = (pos - bMin) / bSpan;

        if (channel == 1 || quantRes > 0) {
            float qN = quantRes > 0 ? float(quantRes) : gRes();
            uvw = (floor(uvw * qN) + 0.5) / qN;
        }

        vec4 th = sample3DLinear(uTex0, uvw);

        // Diagnostic channels (2..7)
        if (!compositeWorld && channel >= 2 && channel <= 7) {
            vec3 dCol = vec3(0.0);
            float dVal = 0.0;
            if (channel == 2)      { dVal = th.r * 0.45; dCol = vec3(dVal); }
            else if (channel == 3) { dVal = th.g * 0.45; dCol = blackbodyPalette(th.g, 1.15, 0); }
            else if (channel == 4) { dVal = th.b * 0.45; dCol = vec3(0.15, 0.65, 1.0) * dVal; }
            else if (channel == 5) { vec3 v3 = sample3DLinear(uTex1, uvw).xyz; dVal = length(v3) * 0.18; dCol = abs(v3) * 0.22; }
            else if (channel == 6) { vec4 c4 = sample3DLinear(uTex3, uvw); dVal = c4.w * 0.15; dCol = abs(c4.xyz) * 0.20; }
            else if (channel == 7) { vec2 pi = sample3DLinear(uTex2, uvw).rg; dVal = (abs(pi.r) + pi.g) * 0.8; dCol = vec3(abs(pi.r) * 2.5, pi.g * 1.6, abs(pi.r)); }

            if (dVal > 0.004) {
                float a = 1.0 - exp(-dVal * 3.5 * stepLen);
                radiance += transmittance * dCol * a * 2.2;
                transmittance *= (1.0 - a);
            }
            t += stepLen;
            continue;
        }

        float smoke = clamp(th.r, 0.0, 16.0);
        float temp  = clamp(th.g, 0.0, 14.0);
        float react = clamp(th.a, 0.0, 16.0);
        if (smoke > 0.004 || temp > 0.02) {
            float effStep    = stepLen * opticalScale;
            float extinction = max(0.001, smoke * uOpticsParams0.y + temp * 0.65);
            float segT       = exp(-extinction * effStep);

            // Sun shadow march through volume
            float tauSun = 0.0;
            float shStep = 0.075;
            for (int s = 1; s <= 16; ++s) {
                if (s > shadowSteps) break;
                vec3 sUvw = uvw + sunDir * (float(s) * shStep);
                if (any(lessThan(sUvw, vec3(0.0))) || any(greaterThan(sUvw, vec3(1.0)))) break;
                tauSun += clamp(sample3DLinear(uTex0, sUvw).r, 0.0, 16.0) * shStep;
            }
            float sunTrans = exp(-tauSun * uOpticsParams1.w);
            float powder   = 1.0 - 0.45 * exp(-smoke * uOpticsParams0.y * 2.2);

            vec3 sunLight = vec3(1.0, 0.93, 0.82) * uSunDirAndIntensity.w * sunTrans * phase * powder * 2.4;
            vec3 skyAmb   = vec3(0.30, 0.38, 0.52) * uOpticsParams0.x * mix(0.45, 1.15, clamp(uvw.y, 0.0, 1.0));

            float irrad = clamp(sample3DLinear(uTex2, uvw).g, 0.0, 4.0);
            vec3 internalFireLight = blackbodyPalette(max(0.75, irrad * 1.25), uOpticsParams1.x, palette)
                                   * irrad * uOpticsParams1.z * 0.35 * (0.45 + 0.55 * exp(-smoke * 0.9));

            vec3 smokeScattered = uOpticsParams0.z * (sunLight + skyAmb) + internalFireLight;
            vec3 fireEmission   = blackbodyPalette(temp, uOpticsParams1.x, palette) * uOpticsParams0.w * 0.52;

            vec3 totalSource  = smokeScattered * (smoke * uOpticsParams0.y)
                              + fireEmission * (1.0 + react * 0.18);
            vec3 stepIntegral = totalSource * ((1.0 - segT) / extinction);

            radiance += transmittance * stepIntegral;
            transmittance *= segT;
        }
        t += stepLen;
    }

    float alpha = clamp(1.0 - transmittance, 0.0, 1.0);
    if (compositeWorld) {
        // Output linear-ish tone-mapped premultiplied RGBA for blending (ONE, ONE_MINUS_SRC_ALPHA) over world FBO!
        vec3 mapped = acesTonemap(radiance * uOpticsParams2.y);
        outColor = vec4(mapped, alpha);
    } else if (transparentBg) {
        vec3 mapped = acesTonemap(radiance * uOpticsParams2.y);
        outColor = vec4(mapped, alpha);
    } else {
        vec3 combined = radiance + transmittance * bgCol;
        vec3 mapped   = acesTonemap(combined * uOpticsParams2.y);
        outColor = vec4(pow(max(mapped, vec3(1e-5)), vec3(1.0 / 2.2)), 1.0);
    }
}
"""

PYRO_EMBERS_VERT = "#version 450\n" + PYRO_UBO_GLSL + r"""
layout(location = 0) in float aSeed;
layout(location = 0) out vec4 vEmberColor;

void main() {
    float life  = max(0.3, uEmberAndActiveParams.w);
    float phase = fract(aSeed * 17.37 + uTimeAndFlags.x / life);
    vec3 h      = hash33(vec3(aSeed * 91.7, floor(uTimeAndFlags.x / life + aSeed * 17.37), aSeed * 31.1));

    vec3 uvw = vec3(0.5 + (h.x - 0.5) * 0.36, 0.08 + phase * 0.82, 0.5 + (h.z - 0.5) * 0.36);
    vec3 flow = sample3DLinear(uTex1, uvw).xyz;
    uvw.xz += flow.xz * phase * 0.08;
    uvw = clamp(uvw, vec3(0.02), vec3(0.98));

    vec4 th = sample3DLinear(uTex0, uvw);
    float activity = clamp(th.g * 0.45 + th.r * 0.2, 0.0, 1.5);

    vec3 worldPos = mix(uDomainBoxMin.xyz, uDomainBoxMax.xyz, uvw);
    vec3 rel = worldPos - uCamPosAndFov.xyz;
    float zCam = dot(rel, uCamForwardAndComp.xyz);

    if (zCam <= 0.12 || activity < 0.05) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        vEmberColor = vec4(0.0);
        return;
    }

    float xCam = dot(rel, uCamRightAndTransp.xyz);
    float yCam = dot(rel, uCamUpAndChannel.xyz);
    float tanH = uCamPosAndFov.w;
    float asp  = uDomainBoxMax.w;

    float ndcX = xCam / (zCam * asp * tanH);
    float ndcY = -yCam / (zCam * tanH); // Vulkan Y-down clip space!
    float nearP = max(0.05, uEmberExtraParams.z);
    float farP  = max(20.0, uEmberExtraParams.w);
    float ndcZ  = (farP / (farP - nearP)) - (farP * nearP / (farP - nearP)) / zCam;

    gl_Position  = vec4(ndcX, ndcY, clamp(ndcZ, 0.0, 1.0), 1.0);
    gl_PointSize = clamp(uEmberAndActiveParams.y * (uEmberExtraParams.y / 650.0) * (2.6 / zCam), 1.5, 14.0);

    float fade = sin(phase * 3.14159265) * clamp(activity, 0.0, 1.0);
    vec3 hot   = mix(vec3(1.0, 0.42, 0.08), vec3(1.0, 0.86, 0.48), 1.0 - phase);
    vec3 ash   = vec3(0.25, 0.24, 0.23);
    bool isAsh = (fract(aSeed * 53.1) < uEmberExtraParams.x);
    bool transpExport = (uCamRightAndTransp.w > 0.5 && uCamForwardAndComp.w < 0.5);
    float outAlpha = isAsh ? (fade * 0.55) : (transpExport ? (fade * 0.85) : 0.0);
    vEmberColor = vec4((isAsh ? ash : hot * uEmberAndActiveParams.z) * fade, outAlpha);
}
"""

PYRO_EMBERS_FRAG = r"""#version 450
layout(location = 0) in vec4 vEmberColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;
    float radial = exp(-r2 * 2.8);
    outColor = vec4(vEmberColor.rgb * radial, vEmberColor.a * radial);
}
"""

# ---------------------------------------------------------------------------------------------------------------------
#                                    3D SDF WORLD & .FACET VEHICLE SHADERS
# ---------------------------------------------------------------------------------------------------------------------

WORLD_UBO_GLSL = r"""
layout(std140, set = 0, binding = 0) uniform WorldUniformBlock {
    mat4 uViewProj;
    mat4 uModelMat;
    vec4 uCamEyeAndFov;        // xyz=eye, w=tanHalfFov
    vec4 uCamForwardAndAspect; // xyz=forward, w=aspect
    vec4 uCamRightAndCourse;   // xyz=right, w=provingGrounds (1 or 0)
    vec4 uCamUpAndObstacles;   // xyz=up, w=obstaclesEnabled (1 or 0)
    vec4 uSunDirAndIntensity;  // xyz=sunDir, w=sunIntensity
    vec4 uEnvParams;           // x=ambient, y=exposure, z=nearPlane, w=farPlane
    vec4 uMeshTintAndOpacity;  // xyz=paintRgb, w=faceOpacity
    vec4 uMeshScanParams;      // x=viewMode, y=scanFrac, z=boundsMinX, w=boundsMaxX
    vec4 uMeshThermalParams;   // x=thermalGlow, y=fireLightCount
    vec4 uFireLightPosRad[8];  // xyz=worldPos, w=radius
    vec4 uFireLightColor[8];   // xyz=linearRadiance
};
"""

WORLD_SDF_RAYMARCH_FRAG = "#version 450\n" + WORLD_UBO_GLSL + r"""
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outWorldColor;
layout(location = 1) out vec4 outLinearDepth;

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdCylinderZ(vec3 p, float r, float h) {
    vec2 d = abs(vec2(length(p.xy), p.z)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// Returns vec2(signedDistance, materialId)
// matId: 1=ground/asphalt, 2=kerb, 3=ramp, 4=sdfSphere, 5=pillar/arch, 6=cone, 7=lobbyFloor
vec2 mapWorld(vec3 p) {
    bool proving = (uCamRightAndCourse.w > 0.5);
    if (!proving) {
        float floorD = p.z;
        float ped    = sdCylinderZ(p - vec3(0.0, 0.0, 0.02), 3.6, 0.02);
        return (ped < floorD) ? vec2(ped, 7.0) : vec2(floorD, 7.0);
    }

    vec2 res = vec2(p.z, 1.0);

    // Raised kerbs along lane edges |y| in [6.0 .. 6.3]
    float kerbY = abs(p.y) - 6.15;
    float kerbD = sdBox(vec3(p.x + 4.0, kerbY, p.z - 0.06), vec3(48.0, 0.15, 0.06));
    if (kerbD < res.x) res = vec2(kerbD, 2.0);

    // Test ramp X=[22..28], |Y|<=3.2, Rise=1.35
    if (abs(p.x - 25.0) < 4.5 && abs(p.y) < 4.2) {
        float rampFrac = clamp((p.x - 22.0) / 6.0, 0.0, 1.0);
        float rampTop  = rampFrac * 1.35;
        float rampBox  = max(max(abs(p.x - 25.0) - 3.0, abs(p.y) - 3.2), max(-p.z, p.z - rampTop));
        if (rampBox < res.x) res = vec2(rampBox, 3.0);
    }

    if (uCamUpAndObstacles.w > 0.5) {
        // 3D SDF Deflector Sphere at (6.2, -2.35, 0.82), R = 0.68
        float sph = length(p - vec3(6.2, -2.35, 0.82)) - 0.68;
        if (sph < res.x) res = vec2(sph, 4.0);

        // Twin SDF Pillars at (9.5, +-3.15, 1.35), R = 0.48
        float pil = sdCylinderZ(vec3(p.x - 9.5, abs(p.y) - 3.15, p.z - 1.35), 0.48, 1.35);
        if (pil < res.x) res = vec2(pil, 5.0);

        // Overhead SDF Portal Arch at X = 15.2
        float leg = sdBox(vec3(p.x - 15.2, abs(p.y) - 4.4, p.z - 1.9), vec3(0.42, 0.42, 1.9));
        float lintel = sdBox(p - vec3(15.2, 0.0, 3.95), vec3(0.48, 4.85, 0.36));
        float arch = min(leg, lintel);
        if (arch < res.x) res = vec2(arch, 5.0);

        // 3 Slalom traffic cones
        vec2 conePos[3] = vec2[3](vec2(4.2, 1.55), vec2(8.0, -1.25), vec2(13.2, 1.65));
        for (int i = 0; i < 3; ++i) {
            vec3 cp = p - vec3(conePos[i], 0.0);
            float h = clamp(cp.z / 0.48, 0.0, 1.0);
            float cr = mix(0.18, 0.04, h);
            float cd = max(length(cp.xy) - cr, max(-cp.z, cp.z - 0.48));
            if (cd < res.x) res = vec2(cd, 6.0);
        }
    }

    return res;
}

vec3 calcNormal(vec3 p) {
    vec2 e = vec2(0.0035, 0.0);
    return normalize(vec3(
        mapWorld(p + e.xyy).x - mapWorld(p - e.xyy).x,
        mapWorld(p + e.yxy).x - mapWorld(p - e.yxy).x,
        mapWorld(p + e.yyx).x - mapWorld(p - e.yyx).x));
}

float softShadow(vec3 ro, vec3 rd) {
    float res = 1.0;
    float t = 0.06;
    for (int i = 0; i < 28; ++i) {
        float h = mapWorld(ro + rd * t).x;
        if (h < 0.002) return 0.0;
        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.04, 0.85);
        if (t > 28.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

float calcAo(vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 1; i <= 4; ++i) {
        float h = 0.08 * float(i);
        float d = mapWorld(p + h * n).x;
        occ += (h - d) * sca;
        sca *= 0.65;
    }
    return clamp(1.0 - 2.2 * occ, 0.15, 1.0);
}

vec3 acesWorld(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec2 ndc = vec2(vUv.x * 2.0 - 1.0, 1.0 - vUv.y * 2.0);
    float tanH = uCamEyeAndFov.w;
    float asp  = uCamForwardAndAspect.w;
    vec3 ro = uCamEyeAndFov.xyz;
    vec3 rd = normalize(
        uCamForwardAndAspect.xyz +
        ndc.x * asp * tanH * uCamRightAndCourse.xyz +
        ndc.y * tanH * uCamUpAndObstacles.xyz);

    vec3 sunDir = normalize(uSunDirAndIntensity.xyz);
    bool proving = (uCamRightAndCourse.w > 0.5);

    // Sky gradient
    vec3 skyHorizon = proving ? vec3(0.32, 0.39, 0.48) : vec3(0.055, 0.060, 0.070);
    vec3 skyZenith  = proving ? vec3(0.09, 0.16, 0.29) : vec3(0.018, 0.020, 0.025);
    vec3 skyCol     = mix(skyHorizon, skyZenith, pow(clamp(rd.z, 1e-5, 1.0), 0.65));
    skyCol += vec3(1.0, 0.88, 0.70) * pow(max(dot(rd, sunDir), 1e-5), 96.0) * uSunDirAndIntensity.w * 0.45;

    float t = 0.08;
    float hitMat = 0.0;
    const float maxDist = 155.0;
    for (int i = 0; i < 110; ++i) {
        vec2 m = mapWorld(ro + rd * t);
        if (m.x < 0.0025 * (1.0 + 0.12 * t)) {
            hitMat = m.y;
            break;
        }
        t += m.x * 0.92;
        if (t > maxDist) break;
    }

    if (hitMat < 0.5) {
        vec3 mapped = pow(max(acesWorld(skyCol * uEnvParams.y), vec3(1e-5)), vec3(1.0 / 2.2));
        outWorldColor  = vec4(mapped, 1.0);
        outLinearDepth = vec4(1e6, 0.0, 0.0, 1.0);
        gl_FragDepth   = 1.0;
        return;
    }

    vec3 pos = ro + rd * t;
    vec3 nor = calcNormal(pos);
    vec3 albedo = vec3(0.14, 0.15, 0.16);

    if (hitMat < 1.5) {
        // Proving Grounds asphalt + lane markings + distance markers
        float grain = fract(sin(dot(floor(pos.xy * 18.0), vec2(12.9898, 78.233))) * 43758.5453);
        albedo = vec3(0.105, 0.110, 0.118) * (0.92 + 0.14 * grain);
        if (abs(pos.y) < 5.95) {
            float centerDash = (abs(pos.y) < 0.06 && fract(pos.x * 0.25) < 0.45) ? 1.0 : 0.0;
            float sideLine   = (abs(abs(pos.y) - 5.82) < 0.06) ? 1.0 : 0.0;
            albedo = mix(albedo, vec3(0.72, 0.70, 0.62), max(centerDash, sideLine));
        } else {
            // Outer apron
            albedo = vec3(0.075, 0.082, 0.080);
        }
    } else if (hitMat < 2.5) {
        // Red/white racing kerbs
        float stripe = step(0.5, fract(pos.x * 0.65));
        albedo = mix(vec3(0.76, 0.14, 0.12), vec3(0.82, 0.82, 0.80), stripe);
    } else if (hitMat < 3.5) {
        albedo = vec3(0.20, 0.22, 0.25);
    } else if (hitMat < 4.5) {
        // SDF Deflector Sphere (brushed titanium + warm accent band)
        albedo = abs(pos.z - 0.82) < 0.06 ? vec3(0.86, 0.44, 0.12) : vec3(0.42, 0.46, 0.52);
    } else if (hitMat < 5.5) {
        // SDF Pillars & Portal Arch
        albedo = vec3(0.28, 0.30, 0.34);
    } else if (hitMat < 6.5) {
        // Safety Cones
        albedo = (pos.z > 0.20 && pos.z < 0.32) ? vec3(0.90, 0.90, 0.92) : vec3(0.92, 0.30, 0.05);
    } else {
        // Lobby studio cove & plinth
        float r = length(pos.xy);
        float ring = 1.0 - smoothstep(0.0, 0.03, abs(r - 3.55));
        albedo = mix(vec3(0.085, 0.090, 0.102), vec3(0.32, 0.52, 0.72), ring * 0.65);
    }

    float sh  = proving ? softShadow(pos + nor * 0.02, sunDir) : 0.85;
    float ao  = calcAo(pos, nor);
    float ndl = max(dot(nor, sunDir), 0.0);
    vec3 halfV = normalize(sunDir - rd);
    float spec = pow(max(dot(nor, halfV), 1e-5), 32.0) * sh * (hitMat > 3.5 ? 0.35 : 0.08);

    vec3 lit = albedo * (vec3(1.0, 0.96, 0.90) * uSunDirAndIntensity.w * ndl * sh +
                         vec3(0.52, 0.62, 0.78) * uEnvParams.x * ao) + vec3(spec);

    // Dynamic flickering fire lights (from Camp Fire & Ignited Tyres!)
    int fCount = int(uMeshThermalParams.y + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= fCount) break;
        vec3 toL = uFireLightPosRad[i].xyz - pos;
        float dist = length(toL);
        float rad  = max(0.5, uFireLightPosRad[i].w);
        if (dist < rad * 2.2) {
            vec3 lDir = toL / max(dist, 1e-3);
            float att = (clamp(1.0 - dist / (rad * 2.2), 0.0, 1.0) * clamp(1.0 - dist / (rad * 2.2), 0.0, 1.0)) / (1.0 + 0.35 * dist * dist);
            lit += albedo * uFireLightColor[i].xyz * max(dot(nor, lDir), 0.0) * att * 4.5;
        }
    }

    // Distance aerial perspective
    float fog = 1.0 - exp(-0.00028 * t * t);
    lit = mix(lit, skyHorizon, fog);

    vec3 mapped = pow(max(acesWorld(lit * uEnvParams.y), vec3(1e-5)), vec3(1.0 / 2.2));
    outWorldColor  = vec4(mapped, 1.0);
    outLinearDepth = vec4(t, 0.0, 0.0, 1.0);

    float viewZ = max(uEnvParams.z, dot(pos - ro, uCamForwardAndAspect.xyz));
    float nearP = uEnvParams.z;
    float farP  = uEnvParams.w;
    float depth01 = (farP / (farP - nearP)) - (farP * nearP / (farP - nearP)) / viewZ;
    gl_FragDepth = clamp(depth01, 0.0, 0.9999);
}
"""

WORLD_VEHICLE_VERT = "#version 450\n" + WORLD_UBO_GLSL + r"""
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aBarycentric;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec3 vBarycentric;
layout(location = 3) out float vLocalX;

void main() {
    vec4 wp = uModelMat * vec4(aPosition, 1.0);
    vWorldPos    = wp.xyz;
    vWorldNormal = normalize(mat3(uModelMat) * aNormal);
    vBarycentric = aBarycentric;
    vLocalX      = aPosition.x;
    gl_Position  = uViewProj * wp;
}
"""

WORLD_VEHICLE_FRAG = "#version 450\n" + WORLD_UBO_GLSL + r"""
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec3 vBarycentric;
layout(location = 3) in float vLocalX;

layout(location = 0) out vec4 outWorldColor;
layout(location = 1) out vec4 outLinearDepth;

vec3 acesMesh(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 nor = normalize(vWorldNormal);
    vec3 viewDir = normalize(uCamEyeAndFov.xyz - vWorldPos);
    vec3 sunDir  = normalize(uSunDirAndIntensity.xyz);

    int viewMode = int(uMeshScanParams.x + 0.5);
    vec3 dBary   = fwidth(vBarycentric);
    vec3 edge3   = smoothstep(vec3(0.0), dBary * 1.35, vBarycentric);
    float wire   = 1.0 - min(min(edge3.x, edge3.y), edge3.z);

    if (viewMode == 2 && wire < 0.15) {
        discard;
    }

    vec3 albedo = uMeshTintAndOpacity.xyz;
    float ndl   = max(dot(nor, sunDir), 0.0);
    vec3 halfV  = normalize(sunDir + viewDir);
    float fres  = pow(max(1.0 - max(dot(nor, viewDir), 0.0), 1e-5), 4.0);
    float spec  = pow(max(dot(nor, halfV), 1e-5), 64.0) * 0.55;

    vec3 skyRefl = mix(vec3(0.12, 0.15, 0.20), vec3(0.45, 0.55, 0.72), clamp(nor.z * 0.5 + 0.5, 0.0, 1.0));
    vec3 lit = albedo * (vec3(1.0, 0.96, 0.90) * uSunDirAndIntensity.w * ndl + vec3(0.50, 0.60, 0.76) * uEnvParams.x)
             + skyRefl * fres * 0.38 + vec3(spec);

    // Dynamic flickering fire lights illuminating car body & wheels!
    int fCount = int(uMeshThermalParams.y + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= fCount) break;
        vec3 toL = uFireLightPosRad[i].xyz - vWorldPos;
        float dist = length(toL);
        float rad  = max(0.5, uFireLightPosRad[i].w);
        if (dist < rad * 2.2) {
            vec3 lDir = toL / max(dist, 1e-3);
            float att = (clamp(1.0 - dist / (rad * 2.2), 0.0, 1.0) * clamp(1.0 - dist / (rad * 2.2), 0.0, 1.0)) / (1.0 + 0.35 * dist * dist);
            lit += (albedo + vec3(0.18)) * uFireLightColor[i].xyz * max(dot(nor, lDir), 0.0) * att * 5.2;
        }
    }

    // Overheated / burning tyre tread incandescent glow
    if (uMeshThermalParams.x > 0.01) {
        lit += vec3(1.4, 0.32, 0.04) * pow(max(uMeshThermalParams.x, 1e-5), 1.6) * 1.4;
    }

    // Ghost / Wireframe / X-ray Scan effects
    if (viewMode != 0) {
        lit = mix(lit * 0.45, vec3(0.32, 0.78, 1.0), wire * 0.85);
        if (viewMode == 3) {
            float span = max(0.2, uMeshScanParams.w - uMeshScanParams.z);
            float normX = (uMeshScanParams.w - vLocalX) / span;
            float band = exp(-(((normX - uMeshScanParams.y) * 14.0) * ((normX - uMeshScanParams.y) * 14.0)));
            lit += vec3(0.35, 0.92, 1.0) * band * 1.8;
        }
    }

    vec3 mapped = pow(max(acesMesh(lit * uEnvParams.y), vec3(1e-5)), vec3(1.0 / 2.2));
    float alpha = (viewMode == 0) ? 1.0 : max(uMeshTintAndOpacity.w, wire * 0.92);
    outWorldColor  = vec4(mapped, alpha);

    float tRay = length(vWorldPos - uCamEyeAndFov.xyz);
    outLinearDepth = vec4(tRay, 0.0, 0.0, 1.0);
}
"""

# ---------------------------------------------------------------------------------------------------------------------
#                                        DEAR IMGUI VULKAN SHADERS
# ---------------------------------------------------------------------------------------------------------------------

IMGUI_VERT = r"""#version 450
layout(std140, set = 0, binding = 0) uniform ImGuiUniformBlock {
    mat4 uOrthoProj;
};
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
    vUv = aUv;
    vColor = aColor;
    gl_Position = uOrthoProj * vec4(aPos, 0.0, 1.0);
}
"""

IMGUI_FRAG = r"""#version 450
layout(set = 0, binding = 1) uniform sampler2D uTexture;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor * texture(uTexture, vUv);
}
"""

SHADERS: list[tuple[str, str, str]] = [
    ("FullScreenQuadVert",   "vert", FULLSCREEN_VERT),
    ("PyroClearSingleFrag",  "frag", PYRO_CLEAR_SINGLE_FRAG),
    ("PyroClearDualFrag",    "frag", PYRO_CLEAR_DUAL_FRAG),
    ("PyroSplatFrag",        "frag", PYRO_SPLAT_FRAG),
    ("PyroAdvectFrag",       "frag", PYRO_ADVECT_FRAG),
    ("PyroMacCormackFrag",   "frag", PYRO_MACCORMACK_FRAG),
    ("PyroCurlFrag",         "frag", PYRO_CURL_FRAG),
    ("PyroVorticityFrag",    "frag", PYRO_VORTICITY_FRAG),
    ("PyroBuoyancyFrag",     "frag", PYRO_BUOYANCY_FRAG),
    ("PyroDivergenceFrag",   "frag", PYRO_DIVERGENCE_FRAG),
    ("PyroJacobiFrag",       "frag", PYRO_JACOBI_FRAG),
    ("PyroGradientFrag",     "frag", PYRO_GRADIENT_FRAG),
    ("PyroRaymarchFrag",     "frag", PYRO_RAYMARCH_FRAG),
    ("PyroEmbersVert",       "vert", PYRO_EMBERS_VERT),
    ("PyroEmbersFrag",       "frag", PYRO_EMBERS_FRAG),
    ("WorldSdfRaymarchFrag", "frag", WORLD_SDF_RAYMARCH_FRAG),
    ("WorldVehicleVert",     "vert", WORLD_VEHICLE_VERT),
    ("WorldVehicleFrag",     "frag", WORLD_VEHICLE_FRAG),
    ("ImGuiVulkanVert",      "vert", IMGUI_VERT),
    ("ImGuiVulkanFrag",      "frag", IMGUI_FRAG),
]


def find_glslang(override: str | None) -> str:
    if override and os.path.isfile(override):
        return override
    for candidate in (
        "/tmp/glslang-build/StandAlone/glslang",
        "/tmp/glslang-build/StandAlone/glslangValidator",
        shutil.which("glslang"),
        shutil.which("glslangValidator"),
    ):
        if candidate and os.path.isfile(candidate):
            return candidate
    raise RuntimeError("Could not locate glslang / glslangValidator binary.")


def compile_spirv_words(glslang_bin: str, name: str, stage: str, source: str) -> list[int]:
    with tempfile.TemporaryDirectory() as tmp:
        src_path = Path(tmp) / f"{name}.{stage}"
        spv_path = Path(tmp) / f"{name}.spv"
        src_path.write_text(source, encoding="utf-8")
        proc = subprocess.run(
            [glslang_bin, "-V", "--target-env", "vulkan1.1", "-S", stage, str(src_path), "-o", str(spv_path)],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            raise RuntimeError(f"SPIR-V compilation failed for {name}:\n{proc.stdout}\n{proc.stderr}")
        raw = spv_path.read_bytes()
        if len(raw) % 4 != 0:
            raise RuntimeError(f"Invalid SPIR-V byte length for {name}: {len(raw)}")
        return list(struct.unpack(f"<{len(raw) // 4}I", raw))


def render_header(compiled: list[tuple[str, list[int]]]) -> str:
    lines: list[str] = [
        "#pragma once",
        "/*==========================================================================================================================================",
        "                                                           VULKANSPIRVLIBRARY.H",
        "==========================================================================================================================================*/",
        "// Generated by Tools/Build/GenerateVulkanShaders.py — DO NOT EDIT BY HAND.",
        "// Precompiled Vulkan 1.1 SPIR-V shader modules for PyroVolumeSolver, WorldSdfScene, ControlVehicle, and ImGui.",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace Frontier::VulkanShaders",
        "{",
        "",
        "struct SpirvBinaryView",
        "{",
        "    const uint32_t* Words     = nullptr;",
        "    size_t          WordCount = 0u;",
        "};",
        "",
    ]

    for name, words in compiled:
        lines.append(f"inline constexpr uint32_t {name}Words[{len(words)}] = {{")
        for idx in range(0, len(words), 10):
            chunk = ", ".join(f"0x{w:08x}u" for w in words[idx : idx + 10])
            lines.append(f"    {chunk},")
        lines.append("};")
        lines.append(
            f"inline constexpr SpirvBinaryView {name}{{ {name}Words, {len(words)}u }};"
        )
        lines.append("")

    lines.append("} // namespace Frontier::VulkanShaders")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--glslang", default=None)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    glslang_bin = find_glslang(args.glslang)
    compiled: list[tuple[str, list[int]]] = []
    for name, stage, src in SHADERS:
        words = compile_spirv_words(glslang_bin, name, stage, src)
        compiled.append((name, words))

    header_text = render_header(compiled)
    if args.check:
        if not OUTPUT_HEADER.exists() or OUTPUT_HEADER.read_text(encoding="utf-8") != header_text:
            sys.stderr.write("VulkanSpirvLibrary.h is out of date.\n")
            return 1
        print(f"Verified {len(compiled)} Vulkan 1.1 SPIR-V modules in {OUTPUT_HEADER.relative_to(REPO_ROOT)}.")
        return 0

    OUTPUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_HEADER.write_text(header_text, encoding="utf-8")
    total_kb = sum(len(w) * 4 for _, w in compiled) / 1024.0
    print(f"Generated {len(compiled)} Vulkan 1.1 SPIR-V modules ({total_kb:.1f} KB) -> {OUTPUT_HEADER.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
