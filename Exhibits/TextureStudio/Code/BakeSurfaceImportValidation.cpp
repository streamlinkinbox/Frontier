// 🧩 Exercise native OBJ/STL decoding, error handling and correspondence names.
#include "Engine/ContentInterchange/BakeSurfaceImport.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Frontier;
int main()
{
    const auto Root = std::filesystem::temp_directory_path() / "frontier-bake-import-validation";
    std::filesystem::create_directories(Root);
    auto Save = [&](const char *Name, const std::string &Content) { std::ofstream File(Root / Name, std::ios::binary); File.write(Content.data(), Content.size()); };
    Save("surface.obj", "o Part_low\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf -4/-4 -3/-3 -2/-2 -1/-1\no Other_low\nf 1/1 2/2 3/3\n");
    auto Obj = DecodeBakeSurface(Root / "surface.obj", true);
    if (Obj.Parts.size() != 2 || Obj.Parts[0].TriangleCount() != 2 || Obj.Parts[1].Name != "Other_low" || Obj.Parts[0].Vertices[1].Uv[0] != 1) throw std::runtime_error("OBJ decoding failed");
    Save("islands.obj", "o Islands\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 0.2 0.2\nvt 0.8 0.8\nf 1/1 2/2 3/3\nf 1/5 3/6 4/4\n");
    auto Islands = DecodeBakeSurface(Root / "islands.obj", true);
    if (Islands.Parts[0].Vertices[0].ChartId == Islands.Parts[0].Vertices[3].ChartId) throw std::runtime_error("UV seams must produce separate charts");
    Save("no_uv.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    bool Rejected = false; try { (void)DecodeBakeSurface(Root / "no_uv.obj", true); } catch (const std::exception &) { Rejected = true; }
    if (!Rejected || DecodeBakeSurface(Root / "no_uv.obj", false).Parts.empty()) throw std::runtime_error("UV validation failed");
    Save("ascii.stl", "solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid\n");
    if (DecodeBakeSurface(Root / "ascii.stl", false).Parts[0].TriangleCount() != 1) throw std::runtime_error("ASCII STL failed");
    std::string Binary(134, '\0'); Binary[80] = 1;
    const float Vertices[9]{0,0,0,1,0,0,0,1,0}; std::memcpy(Binary.data()+96, Vertices, sizeof(Vertices)); Save("binary.stl", Binary);
    if (DecodeBakeSurface(Root / "binary.stl", false).Parts[0].TriangleCount() != 1) throw std::runtime_error("Binary STL failed");
    if (BakeCorrespondenceName("Wheel_LOW") != BakeCorrespondenceName("wheel_high") || BakeCorrespondenceName("wheel_hi") == "wheel") throw std::runtime_error("Matching failed");
    Save("invalid.obj", "v 0 0 0\nf 1 2 3\n"); Rejected = false; try { (void)DecodeBakeSurface(Root / "invalid.obj", false); } catch (const std::exception &) { Rejected = true; }
    if (!Rejected) throw std::runtime_error("Invalid index accepted");
    std::filesystem::remove_all(Root);
    std::cout << "PASS: OBJ groups, negative indices, UV validation, ASCII/binary STL and name normalization\n";
}
