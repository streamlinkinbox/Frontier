#!/usr/bin/env bash
# Sandbox proof for ShowroomStructure: constructs the level in-process and asserts the geometry invariants
#    (normal/triangle parity, material range, room bounds, no degenerates, unit normals, luminaire-last).
#
# This clone is missing Engine/GeometricRaster/GeometryStructure.h and Engine/DeviceExchange/OrientationClassifier.h
#    (the existing ShaderBallStructure.cpp fails to compile here for the same reason), so the script stages minimal
#    stubs for exactly those two headers. Everything else is the real engine source.
set -euo pipefail
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
Vkh="${1:-/tmp/vkh/include}"
Work="$(mktemp -d)"; trap 'rm -rf "$Work"' EXIT
cp -r "$Root/Engine" "$Work/Engine"
mkdir -p "$Work/Projects/Project-Zero/Source"
cp "$Root/Projects/Project-Zero/Source/ShowroomStructure.h" "$Root/Projects/Project-Zero/Source/ShowroomStructure.cpp" "$Work/Projects/Project-Zero/Source/"
cat > "$Work/Engine/GeometricRaster/GeometryStructure.h" <<'STUB'
#pragma once
#include <cstdint>
#include <vector>
namespace Frontier {
struct VertexRecord { float SpatialLocation[3]; float CornerNormal[3]; float TextureU, TextureV; };
struct GeometryStructure { std::vector<VertexRecord> Vertices; };
}
STUB
cat > "$Work/Engine/DeviceExchange/OrientationClassifier.h" <<'STUB'
#pragma once
#include <cmath>
namespace Frontier {
struct Vector3 {
    float x=0,y=0,z=0;
    Vector3()=default; Vector3(float a,float b,float c):x(a),y(b),z(c){}
    Vector3 operator-(const Vector3&o) const {return {x-o.x,y-o.y,z-o.z};}
    Vector3 operator+(const Vector3&o) const {return {x+o.x,y+o.y,z+o.z};}
    Vector3 operator*(float s) const {return {x*s,y*s,z*s};}
    Vector3 operator/(float s) const {return {x/s,y/s,z/s};}
    float Length() const {return std::sqrt(x*x+y*y+z*z);}
    Vector3 Normalized() const {float l=Length(); return l>0?(*this)/l:*this;}
};
struct Matrix4x4 { float Columns[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}; };
struct OrientationClassifier {
    static Vector3 CrossProduct(const Vector3&a,const Vector3&b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
};
}
STUB
cp "$Root/Scratchpad/ShowroomGeometryProof.cpp" "$Work/check.cpp"
g++ -std=c++20 -I "$Vkh" -I "$Work" "$Work/check.cpp" "$Work/Projects/Project-Zero/Source/ShowroomStructure.cpp" -o "$Work/check"
"$Work/check"
echo "[Showroom] OK"
