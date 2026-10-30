/*============================================================================================================================================
                                                      FACETAREATRANSPORT.CPP
============================================================================================================================================*/
// 🧩 Preserve shader ray-aligned edge arithmetic over the existing packed triangle BVH and generate fresh diffuse paths.
#include "FacetAreaTransport.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
using RayVector = std::array<float, 3>;
float Dot(const RayVector& A, const RayVector& B) noexcept { return (A[0]*B[0]+A[1]*B[1])+A[2]*B[2]; }
bool Finite(const RayVector& A) noexcept
{
    return std::isfinite(A[0]) && std::isfinite(A[1]) && std::isfinite(A[2]);
}
RayVector Unit(const RayVector& A) noexcept
{
    const float Squared = Dot(A,A);
    const float Reciprocal = 1.f / std::sqrt(Squared);
    return {A[0]*Reciprocal,A[1]*Reciprocal,A[2]*Reciprocal};
}

bool BoxHit(const TriangleVisibilitySpace::FloatEnclosure& Box, const RayVector& Origin,
            const RayVector& Direction, float Far) noexcept
{
    float Near = 0;
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        if (std::abs(Direction[Axis]) < 1e-30f)
        {
            if (Origin[Axis] < Box.Minimum[Axis] || Origin[Axis] > Box.Maximum[Axis]) return false;
        }
        else
        {
            const float A = (Box.Minimum[Axis]-Origin[Axis])/Direction[Axis];
            const float B = (Box.Maximum[Axis]-Origin[Axis])/Direction[Axis];
            Near = std::max(Near,std::min(A,B));Far = std::min(Far,std::max(A,B));
            if (Far < Near) return false;
        }
    }
    return Far >= Near;
}

bool FacetHit(const FacetAreaTriangle& Triangle, const RayVector& Origin, const RayVector& Direction,
              float Minimum, float Maximum, RayVector& Strike) noexcept
{
    uint32_t Kz = std::abs(Direction[0]) > std::abs(Direction[1]) ? 0 : 1;
    if (std::abs(Direction[2]) > std::abs(Direction[Kz])) Kz = 2;
    if (std::abs(Direction[Kz]) < 1e-20f) return false;
    uint32_t Kx = (Kz+1)%3, Ky = (Kx+1)%3;
    if (Direction[Kz] < 0) std::swap(Kx,Ky);
    RayVector A{},B{},C{};
    for (uint32_t Axis=0;Axis<3;++Axis)
    {
        A[Axis]=Triangle.A[Axis]-Origin[Axis];B[Axis]=Triangle.B[Axis]-Origin[Axis];C[Axis]=Triangle.C[Axis]-Origin[Axis];
    }
    const float Sx=Direction[Kx]/Direction[Kz], Sy=Direction[Ky]/Direction[Kz], Sz=1.f/Direction[Kz];
    const float Ax=A[Kx]-Sx*A[Kz], Ay=A[Ky]-Sy*A[Kz];
    const float Bx=B[Kx]-Sx*B[Kz], By=B[Ky]-Sy*B[Kz];
    const float Cx=C[Kx]-Sx*C[Kz], Cy=C[Ky]-Sy*C[Kz];
    const float U=Cx*By-Cy*Bx, V=Ax*Cy-Ay*Cx, W=Bx*Ay-By*Ax;
    if ((U<0 || V<0 || W<0) && (U>0 || V>0 || W>0)) return false;
    const float Determinant=(U+V)+W;
    if (Determinant==0) return false;
    const float Distance=(((U*A[Kz]+V*B[Kz])+W*C[Kz])*Sz)/Determinant;
    Strike={Distance,V/Determinant,W/Determinant};
    return Distance>Minimum && Distance<Maximum;
}
}

bool FacetAreaTransport::Prepare(const std::vector<FacetAreaShadingTriangle>& Triangles,
                                 const TriangleVisibilitySpace::FloatAtlas& Packed)
{
    Prepared=false;Surfaces.clear();Atlas={};
    if (Triangles.empty() || Packed.Enclosures.empty() || Packed.Corners.size()!=Triangles.size()) return false;
    std::vector<FacetAreaTriangle> Corners;
    for (size_t Index=0;Index<Triangles.size();++Index)
    {
        const auto& Surface=Triangles[Index];
        for (const auto& Normal:Surface.Normals)
            if (!Finite(Normal) || !std::isfinite(Dot(Normal,Normal)) || Dot(Normal,Normal)<=0) return false;
        for (float Channel:Surface.Albedo) if (!std::isfinite(Channel) || Channel<0) return false;
        const auto& T=Surface.Triangle;const auto& P=Packed.Corners[Index];
        for (uint32_t Axis=0;Axis<3;++Axis)
            if (T.A[Axis]!=P.A[Axis] || T.B[Axis]!=P.B[Axis] || T.C[Axis]!=P.C[Axis]) return false;
        Corners.push_back(T);
    }
    FacetAreaProposals AreaCheck;
    if (!AreaCheck.Prepare(Corners)) return false;
    std::vector<uint8_t> Visited(Packed.Enclosures.size()),Covered(Triangles.size());
    std::vector<uint32_t> Pending{0};
    while (!Pending.empty())
    {
        const uint32_t Index=Pending.back();Pending.pop_back();
        if (Index>=Visited.size() || Visited[Index]++) return false;
        const auto& Box=Packed.Enclosures[Index];
        for (uint32_t Axis=0;Axis<3;++Axis)
            if (!std::isfinite(Box.Minimum[Axis]) || !std::isfinite(Box.Maximum[Axis]) ||
                Box.Minimum[Axis]>Box.Maximum[Axis]) return false;
        if (Box.Links[2])
        {
            if (!Box.Links[1] || uint64_t(Box.Links[0])+Box.Links[1]>Triangles.size()) return false;
            for (uint32_t Facet=Box.Links[0];Facet<Box.Links[0]+Box.Links[1];++Facet)
            {
                if (Covered[Facet]++) return false;
                for (const auto& Point:{Triangles[Facet].Triangle.A,Triangles[Facet].Triangle.B,Triangles[Facet].Triangle.C})
                    for (uint32_t Axis=0;Axis<3;++Axis)
                        if (Point[Axis]<Box.Minimum[Axis] || Point[Axis]>Box.Maximum[Axis]) return false;
            }
        }
        else
        {
            for (uint32_t DownLink:{Box.Links[0],Box.Links[1]})
            {
                if (DownLink>=Packed.Enclosures.size()) return false;
                for (uint32_t Axis=0;Axis<3;++Axis)
                    if (Packed.Enclosures[DownLink].Minimum[Axis]<Box.Minimum[Axis] ||
                        Packed.Enclosures[DownLink].Maximum[Axis]>Box.Maximum[Axis]) return false;
                Pending.push_back(DownLink);
            }
        }
    }
    if (std::find(Visited.begin(),Visited.end(),0)!=Visited.end() ||
        std::find(Covered.begin(),Covered.end(),0)!=Covered.end()) return false;
    Surfaces=Triangles;Atlas=Packed;Prepared=true;return true;
}

FacetAreaStrike FacetAreaTransport::Trace(const RayVector& Origin,const RayVector& Direction,
                                         float Minimum,float Maximum) const noexcept
{
    FacetAreaStrike Result;
    if (!Prepared || !Finite(Origin) || !Finite(Direction) || Dot(Direction,Direction)<=0 ||
        !std::isfinite(Dot(Direction,Direction)) || !std::isfinite(Minimum) || !std::isfinite(Maximum) ||
        Minimum<0 || Maximum<=Minimum) return Result;
    Result.Accepted=true;Result.Coordinates={Maximum,0,0};
    std::array<uint32_t,64> Stack{};uint32_t Count=1;
    auto Inspect=[&](uint32_t Index)
    {
        RayVector Candidate{};
        if (FacetHit(Surfaces[Index].Triangle,Origin,Direction,Minimum,Result.Coordinates[0],Candidate))
        { Result.Index=Index;Result.Coordinates=Candidate; }
    };
    while (Count)
    {
        const auto& Box=Atlas.Enclosures[Stack[--Count]];
        if (!BoxHit(Box,Origin,Direction,Result.Coordinates[0])) continue;
        if (Box.Links[2])
            for (uint32_t Index=0;Index<Box.Links[1];++Index) Inspect(Box.Links[0]+Index);
        else
        {
            if (Count+2>Stack.size())
            {
                for (uint32_t Index=0;Index<Surfaces.size();++Index) Inspect(Index);
                return Result;
            }
            Stack[Count++]=Box.Links[0];Stack[Count++]=Box.Links[1];
        }
    }
    return Result;
}

FacetAreaSurface FacetAreaTransport::Resolve(uint32_t Index,float U,float V,const RayVector& Position,
                                            const RayVector& Direction) const noexcept
{
    FacetAreaSurface Surface;
    if (!Prepared || Index>=Surfaces.size() || !std::isfinite(U) || !std::isfinite(V) || U<0 || V<0 ||
        U+V>1.f+2e-6f || !Finite(Position) || !Finite(Direction)) return Surface;
    const auto& T=Surfaces[Index];RayVector Normal{};
    for (uint32_t Axis=0;Axis<3;++Axis)
        Normal[Axis]=(T.Normals[0][Axis]*((1.f-U)-V)+T.Normals[1][Axis]*U)+T.Normals[2][Axis]*V;
    if (Dot(Normal,Normal)<=0 || !std::isfinite(Dot(Normal,Normal))) return Surface;
    Normal=Unit(Normal);
    if (Dot(Normal,Direction)>0) for (float& Coordinate:Normal) Coordinate=-Coordinate;
    Surface.Accepted=true;Surface.Index=Index;Surface.Position=Position;Surface.Normal=Normal;Surface.Albedo=T.Albedo;
    return Surface;
}

FacetAreaSurface FacetAreaTransport::Primary(const RayVector& Origin,const RayVector& Direction) const noexcept
{
    const auto Hit=Trace(Origin,Direction,.00001f,100.f);
    if (!Hit.Accepted || Hit.Index==UINT32_MAX) return {};
    RayVector Position{};
    for (uint32_t Axis=0;Axis<3;++Axis) Position[Axis]=Origin[Axis]+Direction[Axis]*Hit.Coordinates[0];
    auto Surface=Resolve(Hit.Index,Hit.Coordinates[1],Hit.Coordinates[2],Position,Direction);
    Surface.Strike=Hit.Coordinates;return Surface;
}

FacetAreaSurface FacetAreaTransport::Source(const FacetAreaProposal& Proposal) const noexcept
{
    if (!Prepared || !Proposal.Accepted || Proposal.Index>=Surfaces.size()) return {};
    const auto& T=Surfaces[Proposal.Index];RayVector Position{},Direction{};
    for (uint32_t Axis=0;Axis<3;++Axis)
    {
        Position[Axis]=(T.Triangle.A[Axis]+Proposal.U*(T.Triangle.B[Axis]-T.Triangle.A[Axis]))+
                       Proposal.V*(T.Triangle.C[Axis]-T.Triangle.A[Axis]);
        Direction[Axis]=-T.Normals[0][Axis];
    }
    return Resolve(Proposal.Index,Proposal.U,Proposal.V,Position,Direction);
}

FacetAreaPath FacetAreaTransport::Connect(const FacetAreaProposal& Proposal,const FacetAreaSurface& Receiver) const noexcept
{
    FacetAreaPath Path;const auto Surface=Source(Proposal);
    if (!Surface.Accepted || !Receiver.Accepted) return Path;
    FacetAreaScoreInput Input;Input.Triangle=Surfaces[Proposal.Index].Triangle;Input.U=Proposal.U;Input.V=Proposal.V;
    Input.SourceNormal=Surface.Normal;Input.SourceAlbedo=Surface.Albedo;Input.Receiver=Receiver.Position;
    Input.ReceiverNormal=Receiver.Normal;Input.ReceiverAlbedo=Receiver.Albedo;
    const auto Score=EvaluateFacetAreaScore(Input);if (!Score.Accepted) return Path;
    RayVector Delta{};
    for (uint32_t Axis=0;Axis<3;++Axis)
    {
        Path.Origin[Axis]=Surface.Position[Axis]+Surface.Normal[Axis]*.002f;
        const float Target=Receiver.Position[Axis]+Receiver.Normal[Axis]*.002f;
        Delta[Axis]=Target-Path.Origin[Axis];
    }
    const float Distance=std::sqrt(Dot(Delta,Delta));
    if (!std::isfinite(Distance) || Distance<=.00002f) return {};
    for (uint32_t Axis=0;Axis<3;++Axis) Path.Direction[Axis]=Delta[Axis]/Distance;
    Path.Maximum=Distance-.00001f;
    const RayVector Light{.8f,0,.6f};
    const auto Suffix=Trace(Path.Origin,Light,.00001f,100.f);
    const auto Connection=Trace(Path.Origin,Path.Direction,.00001f,Path.Maximum);
    if (!Suffix.Accepted || !Connection.Accepted) return {};
    Path.SuffixVisible=Suffix.Index==UINT32_MAX;Path.ConnectionVisible=Connection.Index==UINT32_MAX;
    const RayVector Radiance{1,.7f,.4f};const float Cosine=std::max(0.f,Dot(Surface.Normal,Light));
    for (uint32_t Axis=0;Axis<3;++Axis)
    {
        const float Outgoing=(Surface.Albedo[Axis]*Radiance[Axis]*Cosine)/3.141592653589793f;
        Path.SuffixOnly[Axis]=(Path.SuffixVisible?Outgoing:0.f)*Score.Geometry[Axis];
        Path.ConnectionOnly[Axis]=Outgoing*Score.Geometry[Axis]*(Path.ConnectionVisible?1.f:0.f);
        Path.Rgb[Axis]=Path.SuffixOnly[Axis]*(Path.ConnectionVisible?1.f:0.f);
        if (!std::isfinite(Path.Rgb[Axis])) return {};
    }
    Path.Accepted=true;return Path;
}
}
