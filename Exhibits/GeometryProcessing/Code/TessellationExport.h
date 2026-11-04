/*====================================================================================================================================
                                                        TESSELLATIONEXPORT.H
====================================================================================================================================*/
// 🧩 Tessellation → JSON (positions, triangles, per-face carrier labels, shared B-rep edge samplings) for the showcase.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Geometry/SurfaceGeometry.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"
#include "Engine/GeometryProcessing/Topology/ModelPartition.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier::GeometryProcessing
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Writes Directory/Code.json; skipped when the file cannot be opened.
inline void ExportTessellation(const ModelPartition& Partition, const TessellationOutcome& Outcome, const std::string& Directory, const std::string& Code)
{
    const std::string Path = Directory + "/" + Code + ".json";
    std::FILE*        File = std::fopen(Path.c_str(), "w");
    if (File == nullptr) return;
    const PolygonComplex& Complex = Outcome.Complex;
    std::fprintf(File, "{\"Code\":\"%s\",\"Positions\":[", Code.c_str());
    for (std::size_t Index = 0; Index < Complex.Positions.size(); ++Index)
    {
        const Vector3& Position = Complex.Positions[Index];
        std::fprintf(File, "%s%.7g,%.7g,%.7g", Index == 0 ? "" : ",", Position.X, Position.Y, Position.Z);
    }
    std::fprintf(File, "],\"Triangles\":[");
    for (std::size_t Index = 0; Index < Complex.Triangles.size(); ++Index)
    {
        const std::array<int, 3>& Corners = Complex.Triangles[Index];
        std::fprintf(File, "%s%d,%d,%d", Index == 0 ? "" : ",", Corners[0], Corners[1], Corners[2]);
    }
    std::fprintf(File, "],\"TriangleFaces\":[");
    for (std::size_t Index = 0; Index < Complex.TriangleFaces.size(); ++Index) std::fprintf(File, "%s%d", Index == 0 ? "" : ",", Complex.TriangleFaces[Index]);
    std::fprintf(File, "],\"Faces\":[");
    for (std::size_t Index = 0; Index < Complex.Faces.size(); ++Index)
    {
        const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Complex.Faces[Index].Face).Surface);
        std::fprintf(File, "%s\"%s\"", Index == 0 ? "" : ",", SurfaceCategoryLabel(Surface));
    }
    std::fprintf(File, "],\"Edges\":[");
    for (std::size_t Index = 0; Index < Complex.Edges.size(); ++Index)
    {
        std::fprintf(File, "%s[", Index == 0 ? "" : ",");
        const std::vector<int>& Vertices = Complex.Edges[Index].Vertices;
        for (std::size_t Sample = 0; Sample < Vertices.size(); ++Sample) std::fprintf(File, "%s%d", Sample == 0 ? "" : ",", Vertices[Sample]);
        std::fprintf(File, "]");
    }
    std::fprintf(File, "]}\n");
    std::fclose(File);
}

}
