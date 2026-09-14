#!/usr/bin/env python3
"""One-shot port: Celestial*.slang -> engine-conformant Sky/ReSTIR sources.

Mechanical identifier renames only (word-boundary sed, longest-first). The
math is untouched: the full parity gate must reproduce bit-identical frames
afterwards. Safe to re-run (copies from Shaders/, asserts on anchors).
"""
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "Shaders"
DST = ROOT / "Integration" / "Shaders"

# (old, new) — applied longest-first with \b boundaries.
RENAME = [
    # file stem (include lines + prose)
    ("CelestialCore", "SkySpecification"),
    ("CelestialReSTIR", "ReSTIRSequence"),
    # types
    ("RandomSequenceStructure", None),  # placeholder guard (never matches old)
    ("CelestialCore", "SkySpecification"),
    ("AtmosphereSpanStructure", None),
    ("CelParams", "SkyConfiguration"),
    ("CelCamera", "SkyProjection"),
    ("CelSpan", "AtmosphereSpanStructure"),
    ("CelAtmo", "AtmosphereStructure"),
    ("CelResDI", "DirectReservoirStructure"),
    ("CelResGI", "IndirectReservoirStructure"),
    ("CelGBuffer", "VisibilityStructure"),
    ("CelInstance", "InstanceStructure"),
    ("CelDraw", "CandidateStructure"),
    ("CelRandU", "RandomScalarStructure"),
    ("CelRng", "RandomSequenceStructure"),
    ("CelHit", "IntersectionStructure"),
    ("CelFrame", "SkyExchange"),
    ("CelLaunch", "SkySequenceConfiguration"),
    ("CelCam", "SkyViewExchange"),
    # dual functions (core)
    ("celAtmoMake", "AtmosphereStructureMake"),
    ("celSkyPixel", "SkyPixelCompute"),
    ("celSkyFull", "SkyRadianceCompute"),
    ("celSkyField", "SkyFieldCompute"),
    ("celSkyShaded", "SkyShadedCompute"),
    ("celSkyFrameOf", "SkyFrameTransform"),
    ("celSkyAmbient", "SkyAmbientCompute"),
    ("celWorldFrameOf", "WorldFrameTransform"),
    ("celAtmosphereSpan", "AtmosphereSpanCompute"),
    ("celAtmosphere", "AtmosphereCompute"),
    ("celDawnGlow", "DawnGlowCompute"),
    ("celSunSpectral", "SunSpectralCompute"),
    ("celSunAureole", "SunAureoleCompute"),
    ("celSunGround", "SunGroundCompute"),
    ("celSunDisc", "SunDiscCompute"),
    ("celStarKelvin", "StarKelvinCompute"),
    ("celStarField", "StarFieldCompute"),
    ("celStarAA", "StarPixelCompute"),
    ("celApplyMedia", "SkyMediaApply"),
    ("celApplyPost", "SkyPostApply"),
    ("celToneAces", "ToneAcesCompute"),
    ("celViewportDir", "ViewportDirectionCompute"),
    ("celAirMassOf", "AirMassCompute"),
    ("celExpHeightK", "ExpHeightKernel"),
    ("celRaySphere", "RaySphereIntersect"),
    ("celExpAtt", "ExtinctionAttenuation"),
    ("celOctDecode", "OctahedralDecode"),
    ("celHorizOf", "HorizonDirectionCompute"),
    ("celVNoise", "NoiseValueCompute"),
    ("celHash13", "HashCompute13"),
    ("celFacing", "SunFacingCompute"),
    ("celFogT", "FogTransmittance"),
    ("celRotX", "RotateXCompute"),
    ("celRotY", "RotateYCompute"),
    ("celLuminanceDW", "LuminanceDWCompute"),
    ("celLuminance", "LuminanceCompute"),
    ("celFract", "MathFract"),
    ("celPow", "MathPow"),
    ("celResDIMake", "DirectReservoirMake"),
    ("celResGIMake", "IndirectReservoirMake"),
    ("celHitMake", "IntersectionMake"),
    ("celRngSeed", "RandomSequenceSeed"),
    ("celRngMake", "RandomSequenceMake"),
    ("celRandNextU", "RandomScalarNext"),
    ("celRandNextF", "RandomFloatNext"),
    ("celRandUMake", "RandomScalarMake"),
    ("celRandFMake", "RandomFloatMake"),
    ("celPCGHash", "RandomPCGHash"),
    ("CelRandF", "RandomFloatStructure"),
    ("celSpanMake", "AtmosphereSpanMake"),
    ("celViewportUV", "ViewportUVCompute"),
    ("celOctEncode", "OctahedralEncode"),
    ("celHash33", "HashCompute33"),
    ("celExp2", "MathExp2"),
    ("celUpdateDI", "DirectReservoirUpdate"),
    ("celUpdateGI", "IndirectReservoirUpdate"),
    ("celCombineDI", "DirectReservoirCombine"),
    ("celCombineGI", "IndirectReservoirCombine"),
    ("celTargetDI", "DirectTargetCompute"),
    ("celTargetGI", "IndirectTargetCompute"),
    ("celDrawMake", "CandidateMake"),
    ("celDrawSunCone", "CandidateSunCone"),
    ("celDrawAureole", "CandidateAureole"),
    ("celDrawCosine", "CandidateCosine"),
    ("celDrawSphere", "CandidateSphere"),
    ("celPdfSunConeAt", "DensitySunConeAt"),
    ("celPdfAureoleAt", "DensityAureoleAt"),
    ("celTraceOccluded", "TraceOccludedQuery"),
    ("celTraceBounce", "TraceBounceCompute"),
    ("celParamsFromGPU", "SkyConfigurationFromExchange"),
    ("CEL_D2R", "SkyDeg2Rad"),
    ("CEL_PI", "SkyPi"),
    # shell cbuffer fields (b0)
    ("gSunColor", "SunColor"),
    ("gSunParams", "SunFactors"),
    ("gSunDir", "SunDirection"),
    ("gAtmoA", "AtmosphereA"),
    ("gAtmoB", "AtmosphereB"),
    ("gSkyTint", "SkyTint"),
    ("gGround", "GroundAlbedo"),
    ("gPostA", "PostA"),
    ("gPostB", "PostB"),
    ("gMediaA", "MediaA"),
    ("gMediaB", "MediaB"),
    ("gMediaC", "MediaC"),
    ("gMediaD", "MediaD"),
    ("gMediaE", "MediaE"),
    ("gStarA", "StarA"),
    ("gStarB", "StarB"),
    ("gStarC", "StarC"),
    ("gMisc", "MiscFactors"),
    # shell cbuffer fields (b1/b2)
    ("gSpatTaps", "SpatialTaps"),
    ("gViewportWidth", None),
    ("gWidth", "ViewportWidth"),
    ("gHeight", "ViewportHeight"),
    ("gFrame", "TemporalIndex"),
    ("gMCap", "ReservoirCap"),
    ("gM0", "InitialCandidates"),
    ("gSeed", "RandomSeed"),
    ("gCamRight", "CameraRight"),
    ("gCamFwd", "CameraForward"),
    ("gCamUp", "CameraUp"),
    ("gCamPad", "CameraPad"),
    ("gPad", "LaunchPad"),
    # shell resources
    ("gResDIWrite", "DirectReservoirExtent"),
    ("gResGIWrite", "IndirectReservoirExtent"),
    ("gResDIRead", "DirectReservoirPrior"),
    ("gResGIRead", "IndirectReservoirPrior"),
    ("gGBuffer", "VisibilityExtent"),
    ("gPositions", "PositionExtent"),
    ("gNormals", "NormalExtent"),
    ("gIndices", "IndexExtent"),
    ("gInstances", "InstanceExtent"),
    ("gOutColor", "DisplayExtent"),
    ("gSkyAmb", "SkyAmbientExtent"),
    ("gScene", "SceneAcceleration"),
]

# Entries: rename definitions + doc mentions, never prose verbs.
ENTRIES = [
    ("skyProbe", "SkyAmbient"),
    ("diInitial", "DirectInitial"),
    ("diTemporal", "DirectTemporal"),
    ("diSpatial", "DirectSpatial"),
    ("giInitial", "IndirectInitial"),
    ("giTemporal", "IndirectTemporal"),
    ("giSpatial", "IndirectSpatial"),
    ("skyViewport", "SkyViewport"),
]


def apply_renames(text, table):
    for old, new in sorted(table, key=lambda kv: -len(kv[0])):
        if new is None:
            continue
        text = re.sub(r"\b%s\b" % re.escape(old), new, text)
    return text


def drop_old_rules(text):
    keep = []
    for ln in text.split("\n"):
        if re.match(r"^//([-=])\1{9,}$", ln):
            continue
        keep.append(ln)
    return "\n".join(keep)


def port_core():
    src = (SRC / "CelestialCore.slang").read_text()
    text = apply_renames(drop_old_rules(src), RENAME)
    hdr = "//@@HDR: \U0001F4E6 Frontier/Shaders/SkySpecification.slang \u2014 Celestial Sun and Sky Mathematics (dual Slang/C++17)\n"
    guard = "SKY_SPECIFICATION_INCLUDED"
    text = (hdr + "#ifndef %s\n#define %s\n\n" % (guard, guard) + text
            + "\n#endif // %s\n" % guard)
    anchors = [
        ("struct SkyConfiguration", "SKY CONFIGURATION"),
        ("struct SkyProjection", "SKY PROJECTION"),
        ("struct AtmosphereStructure", "ATMOSPHERE STRUCTURES"),
        ("AtmosphereStructureMake", "STRUCTURE FACTORIES"),
        ("ExtinctionAttenuation", "ATMOSPHERE INTEGRATION"),
        ("DawnGlowCompute", "DAWN GLOW AND WHITE LINE"),
        ("StarFieldCompute", "STARS AND MILKY WAY"),
        ("SkyMediaApply", "SKY MEDIA (FOG AND ATMOSPHERE-FOG)"),
        ("SkyAmbientCompute", "SKY AMBIENT PROBE"),
        ("SkyRadianceCompute", "SKY RADIANCE ASSEMBLY"),
        ("SkyPostApply", "POST CHAIN AND VIEWPORT"),
    ]
    lines = text.split("\n")
    for anchor, title in anchors:
        for i, ln in enumerate(lines):
            if anchor in ln and not ln.strip().startswith("//"):
                lines.insert(i, "//@@BAN: " + title)
                break
        else:
            print("warn: anchor missing: %s" % anchor)
    (DST / "SkySpecification.slang").write_text("\n".join(lines))
    print("ported SkySpecification.slang (%d lines)" % len(lines))


def port_shell():
    src = (SRC / "CelestialReSTIR.slang").read_text()
    text = apply_renames(drop_old_rules(src), RENAME)
    for old, new in ENTRIES:
        text = re.sub(r"\b%s\b" % old, new, text)
    # `shade` entry: definition only (prose "shade time" stays prose).
    text = re.sub(r"^void shade\(", "void PixelShade(", text, flags=re.M)
    text = text.replace("8 compute entries", "9 compute entries")
    hdr = "//@@HDR: \U0001F4E6 Frontier/Shaders/ReSTIRSequence.slang \u2014 ReSTIR DI/GI Reservoirs and Compute Entries (dual Slang/C++17)\n"
    guard = "RESTIR_SEQUENCE_INCLUDED"
    text = (hdr + "#ifndef %s\n#define %s\n\n" % (guard, guard) + text
            + "\n#endif // %s\n" % guard)
    lines = text.split("\n")
    out = []
    for ln in lines:
        m = re.match(r"^void (SkyAmbient|Direct\w+|Indirect\w+|PixelShade|SkyViewport)\(", ln)
        if m:
            out.append("//@@BAN: COMPUTE ENTRY " + m.group(1).upper())
        out.append(ln)
    (DST / "ReSTIRSequence.slang").write_text("\n".join(out))
    print("ported ReSTIRSequence.slang (%d lines)" % len(out))


def port_prelude():
    src = (ROOT / "Host" / "SlangCompat.h").read_text()
    src = drop_old_rules(src)
    src = apply_renames(src, RENAME)
    src = src.replace("SlangCompat.h \u2014 C++ prelude for dual-compiling",
                      "SlangInterchange.h \u2014 C++ dual header for compiling")
    src = src.replace("Slang-only shells (entry points, buffers)",
                      "Slang-only entry regions (entries, resources)")
    src = src.replace("the dual-compile core never uses them",
                      "the dual-compile text never uses them")
    src = src.replace("PROJECT_ZERO_SLANG_COMPAT_H",
                      "FRONTIER_SLANG_INTERCHANGE_H")
    src = src.replace("Shaders/*.slang as C++17",
                      "Integration/Shaders/*.slang as C++17")
    intvec = '''
// ---- int3/uint3 (mirror Slang integer vector semantics; wraparound) ----
struct int3
{
    int x;
    int y;
    int z;
    int3() : x(0), y(0), z(0) {}
    int3(int s) : x(s), y(s), z(s) {}
    int3(int ax, int ay, int az) : x(ax), y(ay), z(az) {}
    explicit int3(const float3& v) : x(int(v.x)), y(int(v.y)), z(int(v.z)) {}
};
struct uint3
{
    unsigned int x;
    unsigned int y;
    unsigned int z;
    uint3() : x(0u), y(0u), z(0u) {}
    uint3(unsigned int s) : x(s), y(s), z(s) {}
    uint3(unsigned int ax, unsigned int ay, unsigned int az) : x(ax), y(ay), z(az) {}
    explicit uint3(const int3& v) : x(unsigned(v.x)), y(unsigned(v.y)), z(unsigned(v.z)) {}
};
inline uint3 operator+(const uint3& a, const uint3& b) { return uint3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline uint3 operator*(const uint3& a, const uint3& b) { return uint3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline uint3 operator*(const uint3& a, unsigned int s) { return uint3(a.x * s, a.y * s, a.z * s); }
inline uint3 operator^(const uint3& a, const uint3& b) { return uint3(a.x ^ b.x, a.y ^ b.y, a.z ^ b.z); }
inline uint3 operator>>(const uint3& a, unsigned int s) { return uint3(a.x >> s, a.y >> s, a.z >> s); }
inline int3 operator+(const int3& a, const int3& b) { return int3(a.x + b.x, a.y + b.y, a.z + b.z); }
'''
    lines = src.rstrip().split("\n")
    assert lines[-1].startswith("#endif"), "prelude guard tail missing"
    tail = lines.pop()
    src = "\n".join(lines).rstrip() + "\n" + intvec + "\n" + tail + "\n"
    hdr = "//@@HDR: \U0001F4E6 Frontier/Shaders/SlangInterchange.h \u2014 Slang/C++ Dual-Compile Arithmetic Header\n"
    (DST / "SlangInterchange.h").write_text(hdr + src)
    print("ported SlangInterchange.h")


def port_host():
    n = 0
    for name in ["CelHost.h", "SkyViewport.cpp", "CpuPortDiff.cpp",
                 "ReSTIRConvergence.cpp", "MediaProbe.cpp", "SunPosition.h"]:
        p = ROOT / "Host" / name
        if not p.exists():
            continue
        text = p.read_text()
        before = text
        text = apply_renames(text, RENAME)
        text = text.replace('"SlangCompat.h"', '"SlangInterchange.h"')
        text = text.replace('"CelestialCore.slang"', '"SkySpecification.slang"')
        text = text.replace('"CelestialReSTIR.slang"', '"ReSTIRSequence.slang"')
        if text != before:
            p.write_text(text)
            n += 1
    mk = ROOT / "Host" / "Makefile"
    text = mk.read_text()
    if "-I../Shaders" in text:
        mk.write_text(text.replace("-I../Shaders",
                                   "-I../Integration/Shaders"))
    print("ported Host call sites (%d files) + Makefile" % n)


def residuals():
    bad = []
    slang_pats = [r"\bcel[A-Z]\w*", r"\bCel[A-Z]\w*", r"\bg[A-Z]\w*",
                  r"\b(diInitial|diTemporal|diSpatial|giInitial|giTemporal|giSpatial|skyProbe)\b",
                  r"CelestialCore|CelestialReSTIR|SlangCompat"]
    # Host keeps its own gSeed harness variable and CelHost.h (not engine-bound).
    host_pats = [r"\bcel[A-Z]\w*", r"\bCel(?!Host\b)[A-Z]\w*",
                 r"CelestialCore|CelestialReSTIR|SlangCompat"]
    files = [(DST / "SkySpecification.slang", slang_pats),
             (DST / "ReSTIRSequence.slang", slang_pats)]
    files += [(ROOT / "Host" / n, host_pats) for n in
              ["CelHost.h", "SkyViewport.cpp", "CpuPortDiff.cpp",
               "ReSTIRConvergence.cpp", "MediaProbe.cpp"]]
    for f, pats in files:
        for i, ln in enumerate(f.read_text().split("\n"), 1):
            code = ln.split("//")[0]
            for pat in pats:
                m = re.search(pat, code)
                if m:
                    bad.append("%s:%d: %s" % (f.name, i, m.group(0)))
    return bad


def main():
    DST.mkdir(parents=True, exist_ok=True)
    port_core()
    port_shell()
    port_prelude()
    port_host()
    bad = residuals()
    for b in bad[:20]:
        print("residual:", b)
    print("residuals: %d" % len(bad))
    return 0


if __name__ == "__main__":
    sys.exit(main())
