/*============================================================================================================================================
                                                      BAKESURFACEIMPORT.CPP
============================================================================================================================================*/
// 🧩 Decode triangulated OBJ groups and binary or ASCII STL into native bake surfaces.
#include "Engine/ContentInterchange/BakeSurfaceImport.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <tuple>
#include <sstream>
#include <stdexcept>

namespace Frontier
{
namespace
{
using Position = std::array<float, 3>;
using Coordinate = std::array<float, 2>;

void AppendTriangle(BakePart &Part, const std::array<Position, 3> &Positions,
    const std::array<Coordinate, 3> &Coordinates, const std::array<Position, 3> &Normals,
    const std::array<bool, 3> &NormalPresent)
{
    Position A{}, B{}, N{};
    for (int Axis = 0; Axis < 3; ++Axis) { A[Axis] = Positions[1][Axis] - Positions[0][Axis]; B[Axis] = Positions[2][Axis] - Positions[0][Axis]; }
    N = {A[1]*B[2]-A[2]*B[1], A[2]*B[0]-A[0]*B[2], A[0]*B[1]-A[1]*B[0]};
    float Length = std::sqrt(N[0]*N[0]+N[1]*N[1]+N[2]*N[2]);
    if (Length < 1e-20f) throw std::runtime_error("Degenerate triangle in imported surface.");
    for (float &Component : N) Component /= Length;
    for (int Corner = 0; Corner < 3; ++Corner)
    {
        BakeVertex Vertex;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Vertex.Position[Axis] = Positions[Corner][Axis];
            Vertex.Normal[Axis] = NormalPresent[Corner] ? Normals[Corner][Axis] : N[Axis];
        }
        Vertex.Uv[0] = Coordinates[Corner][0]; Vertex.Uv[1] = Coordinates[Corner][1];
        Vertex.ChartId = 1;
        Part.Indices.push_back(uint32_t(Part.Vertices.size()));
        Part.Vertices.push_back(Vertex);
    }
}

// 🧩 Connected UV edges identify islands without welding across texture seams.
void AssignUvCharts(BakePart &Part)
{
    using VertexKey = std::tuple<float, float, float, float, float>;
    using EdgeKey = std::pair<VertexKey, VertexKey>;
    const size_t Count = Part.Indices.size()/3;
    std::vector<size_t> Parent(Count); std::iota(Parent.begin(), Parent.end(), 0);
    const auto Root = [&](size_t Value) { while (Parent[Value] != Value) Value = Parent[Value]; return Value; };
    std::map<EdgeKey, size_t> Owners;
    for (size_t Triangle=0; Triangle<Count; ++Triangle)
    {
        VertexKey Keys[3];
        for (int Corner=0; Corner<3; ++Corner)
        {
            const auto &V=Part.Vertices[Part.Indices[Triangle*3+Corner]];
            Keys[Corner]={V.Position[0],V.Position[1],V.Position[2],V.Uv[0],V.Uv[1]};
        }
        for (int Edge=0; Edge<3; ++Edge)
        {
            EdgeKey Key=std::minmax(Keys[Edge],Keys[(Edge+1)%3]);
            auto [Found, Inserted]=Owners.emplace(Key,Triangle);
            if (!Inserted) { size_t A=Root(Triangle), B=Root(Found->second); if (A!=B) Parent[A]=B; }
        }
    }
    std::map<size_t,uint32_t> Charts;
    for (size_t Triangle=0; Triangle<Count; ++Triangle)
    {
        const size_t Component=Root(Triangle);
        auto Found=Charts.find(Component);
        if (Found==Charts.end()) Found=Charts.emplace(Component,uint32_t(Charts.size()+1)).first;
        for (int Corner=0; Corner<3; ++Corner) Part.Vertices[Part.Indices[Triangle*3+Corner]].ChartId=Found->second;
    }
}

int ResolveIndex(const std::string &Text, size_t Count)
{
    if (Text.empty()) return -1;
    size_t Consumed = 0;
    int Index = std::stoi(Text, &Consumed);
    if (Consumed != Text.size() || Index == 0) throw std::runtime_error("Invalid OBJ index.");
    long long Slot = Index > 0 ? static_cast<long long>(Index) - 1 : static_cast<long long>(Count) + Index;
    if (Slot < 0 || Slot >= static_cast<long long>(Count)) throw std::runtime_error("OBJ index is out of bounds.");
    return int(Slot);
}
struct Corner { int PositionIndex = -1, CoordinateIndex = -1, NormalIndex = -1; };

BakeMeshSet DecodeObj(std::istream &Stream, const std::string &Name, bool RequireUv)
{
    BakeMeshSet Result; std::vector<Position> Positions, Normals; std::vector<Coordinate> Coordinates;
    BakePart Part; Part.Name = Name;
    const auto Flush = [&]() { if (!Part.Indices.empty()) { AssignUvCharts(Part); Result.Parts.push_back(std::move(Part)); Part = {}; } };
    std::string Line;
    while (std::getline(Stream, Line))
    {
        std::istringstream Input(Line); std::string Tag; Input >> Tag;
        if (Tag == "v" || Tag == "vn")
        {
            Position P{}; if (!(Input >> P[0] >> P[1] >> P[2]) || !std::isfinite(P[0]) || !std::isfinite(P[1]) || !std::isfinite(P[2])) throw std::runtime_error("Invalid OBJ vertex or normal.");
            (Tag == "v" ? Positions : Normals).push_back(P);
        }
        else if (Tag == "vt")
        {
            Coordinate P{}; if (!(Input >> P[0] >> P[1]) || !std::isfinite(P[0]) || !std::isfinite(P[1])) throw std::runtime_error("Invalid OBJ UV coordinate."); Coordinates.push_back(P);
        }
        else if (Tag == "o" || Tag == "g")
        {
            std::string Group; std::getline(Input >> std::ws, Group); Flush(); Part.Name = Group.empty() ? Name : Group;
        }
        else if (Tag == "f")
        {
            std::vector<Corner> Face; std::string Token;
            while (Input >> Token)
            {
                if (Token[0] == '#') break;
                size_t First = Token.find('/'), Second = First == std::string::npos ? std::string::npos : Token.find('/', First+1);
                Corner C; C.PositionIndex = ResolveIndex(Token.substr(0, First), Positions.size());
                if (First != std::string::npos) C.CoordinateIndex = ResolveIndex(Token.substr(First+1, Second == std::string::npos ? Second : Second-First-1), Coordinates.size());
                if (Second != std::string::npos) C.NormalIndex = ResolveIndex(Token.substr(Second+1), Normals.size());
                if (C.PositionIndex < 0 || (RequireUv && C.CoordinateIndex < 0)) throw std::runtime_error("Low OBJ requires UV coordinates on every face.");
                Face.push_back(C);
            }
            if (Face.size() < 3) throw std::runtime_error("OBJ face has fewer than three vertices.");
            for (size_t Index = 1; Index+1 < Face.size(); ++Index)
            {
                Corner Corners[]{Face[0], Face[Index], Face[Index+1]};
                std::array<Position, 3> P{}, N{}; std::array<Coordinate, 3> U{}; std::array<bool, 3> HasNormal{};
                for (int At = 0; At < 3; ++At)
                {
                    P[At] = Positions[size_t(Corners[At].PositionIndex)];
                    if (Corners[At].CoordinateIndex >= 0) U[At] = Coordinates[size_t(Corners[At].CoordinateIndex)];
                    if (Corners[At].NormalIndex >= 0) { N[At] = Normals[size_t(Corners[At].NormalIndex)]; HasNormal[At] = true; }
                }
                AppendTriangle(Part, P, U, N, HasNormal);
            }
        }
    }
    Flush(); if (Result.Parts.empty()) throw std::runtime_error("OBJ contains no triangles."); return Result;
}

uint32_t ReadU32(const unsigned char *Bytes) { return uint32_t(Bytes[0]) | uint32_t(Bytes[1])<<8 | uint32_t(Bytes[2])<<16 | uint32_t(Bytes[3])<<24; }
float ReadFloat(const unsigned char *Bytes) { return std::bit_cast<float>(ReadU32(Bytes)); }
BakeMeshSet DecodeStl(const std::string &Bytes, const std::string &Name)
{
    BakePart Part; Part.Name = Name;
    const auto *Raw = reinterpret_cast<const unsigned char *>(Bytes.data());
    const uint32_t Count = Bytes.size() >= 84 ? ReadU32(Raw+80) : 0;
    if (Count && uint64_t(84) + uint64_t(Count)*50 == Bytes.size())
    {
        for (uint32_t Triangle = 0; Triangle < Count; ++Triangle)
        {
            const unsigned char *At = Raw + 84 + size_t(Triangle)*50 + 12;
            std::array<Position,3> P{};
            for (int Corner = 0; Corner < 3; ++Corner) for (int Axis = 0; Axis < 3; ++Axis)
            {
                P[Corner][Axis] = ReadFloat(At); At += 4;
                if (!std::isfinite(P[Corner][Axis])) throw std::runtime_error("Invalid STL vertex.");
            }
            AppendTriangle(Part, P, {}, {}, {});
        }
    }
    else
    {
        std::istringstream Input(Bytes); std::string Word; std::array<Position,3> P{}; int Corner = 0;
        while (Input >> Word) if (Word == "vertex")
        {
            if (!(Input >> P[Corner][0] >> P[Corner][1] >> P[Corner][2])) throw std::runtime_error("Invalid ASCII STL vertex.");
            for (float Coordinate : P[Corner]) if (!std::isfinite(Coordinate)) throw std::runtime_error("Invalid STL vertex.");
            if (++Corner == 3) { AppendTriangle(Part, P, {}, {}, {}); Corner = 0; }
        }
        if (Corner) throw std::runtime_error("Incomplete STL triangle.");
    }
    if (Part.Indices.empty()) throw std::runtime_error("STL contains no triangles.");
    BakeMeshSet Result; Result.Parts.push_back(std::move(Part)); return Result;
}
}

BakeMeshSet DecodeBakeSurface(const std::filesystem::path &Path, bool RequireUv)
{
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File) throw std::runtime_error("Could not open the selected file.");
    auto Size = File.tellg();
    if (Size < 0 || Size > std::streamoff(256*1024*1024)) throw std::runtime_error("Import exceeds the 256 MiB file limit.");
    std::string Bytes(size_t(Size), '\0'); File.seekg(0); if (!File.read(Bytes.data(), Size)) throw std::runtime_error("Could not read the selected file.");
    std::string Extension = Path.extension().string(); std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C){ return char(std::tolower(C)); });
    BakeMeshSet Result;
    if (Extension == ".obj") { std::istringstream Input(Bytes); Result = DecodeObj(Input, Path.stem().string(), RequireUv); }
    else if (Extension == ".stl" && !RequireUv) Result = DecodeStl(Bytes, Path.stem().string());
    else if (Extension == ".stl") throw std::runtime_error("STL has no UV coordinates; use a UV-mapped OBJ for the low target.");
    else throw std::runtime_error("Choose an OBJ or STL file.");
    Result.Label = Path.stem().string(); Result.Source = Path.filename().string(); return Result;
}

std::string BakeCorrespondenceName(const std::string &Name)
{
    std::string Result = Name;
    std::transform(Result.begin(), Result.end(), Result.begin(), [](unsigned char C){ return char(std::tolower(C)); });
    for (const char *Suffix : {"_low", "_high"})
    {
        const std::string Ending(Suffix);
        if (Result.size() >= Ending.size() && Result.compare(Result.size()-Ending.size(), Ending.size(), Ending) == 0)
        { Result.resize(Result.size()-Ending.size()); break; }
    }
    return Result;
}
}
