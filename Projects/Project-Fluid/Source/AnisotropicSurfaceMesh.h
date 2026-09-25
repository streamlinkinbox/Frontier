#pragma once
#include "SurfaceReconstruction.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::ProjectFluid {
struct SurfaceVertex { Vec3 Position; Vec3 Normal; };
struct SurfaceMesh {
    std::vector<SurfaceVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};

// Sparse-brick summed anisotropic field and watertight Marching Cubes surface.
class AnisotropicSurfaceMesh final {
public:
    struct Timings { double IndexMs{},FieldMs{},TrianglesMs{},AssemblyMs{},SmoothingMs{}; };
    const Timings& LastTimings() const noexcept { return Timings_; }
    // Exhaustive mode is retained as a correctness oracle, not a fast path.
    void SetReferenceEvaluation(bool enabled) { Reference_=enabled; ForceRebuild_=true; }
    void Update(const std::vector<SurfaceKernel>& kernels);
    [[nodiscard]] const SurfaceMesh& Mesh() const noexcept { return Mesh_; }
    [[nodiscard]] bool SaveObj(const std::string& path) const;
    [[nodiscard]] std::uint32_t DirtyBrickCount() const noexcept { return DirtyBrickCount_; }
    [[nodiscard]] float IsoValue() const noexcept { return IsoValue_; }
    [[nodiscard]] std::uint32_t OpenEdgeCount() const noexcept { return OpenEdgeCount_; }
    [[nodiscard]] std::uint32_t NonManifoldEdgeCount() const noexcept { return NonManifoldEdgeCount_; }
private:
    static constexpr int Nx=65,Ny=48,Nz=45,Brick=8,Bx=8,By=6,Bz=6;
    static constexpr float Spacing=.07f,IsoValue_=.075f,MinX=-2.24f,MinY=-.16f,MinZ=-1.54f;
    struct EdgeVertex { Vec3 Position; std::uint64_t EdgeKey{}; };
    struct Chunk { std::vector<EdgeVertex> TriangleVertices; };
    [[nodiscard]] static Vec3 GridPosition(int x,int y,int z) noexcept;
    [[nodiscard]] std::size_t At(int x,int y,int z) const noexcept;
    [[nodiscard]] float Evaluate(Vec3 point,const std::vector<SurfaceKernel>& kernels) const noexcept;
    [[nodiscard]] Vec3 Gradient(Vec3 point) const noexcept;
    void MarkKernel(const SurfaceKernel& kernel,std::vector<std::uint8_t>& dirty) const;
    void RebuildFieldBrick(int bx,int by,int bz,const std::vector<SurfaceKernel>& kernels);
    void RebuildMeshBrick(int bx,int by,int bz);
    void AssembleAndSmooth();
    struct PreparedKernel { Vec3 Centre,A,B,C; float Weight; Vec3 Extent; };
    std::vector<PreparedKernel> Prepared_;
    std::array<std::vector<uint32_t>,Bx*By*Bz> Candidates_;
    void Prepare(const std::vector<SurfaceKernel>& kernels);
    Timings Timings_{};
    bool Reference_=false,ForceRebuild_=false;
    std::vector<float> Field_=std::vector<float>(Nx*Ny*Nz);
    std::array<Chunk,Bx*By*Bz> Chunks_{};
    std::vector<SurfaceKernel> Previous_;
    SurfaceMesh Mesh_;
    std::uint32_t DirtyBrickCount_{};
    std::uint32_t OpenEdgeCount_{};
    std::uint32_t NonManifoldEdgeCount_{};
};
}
