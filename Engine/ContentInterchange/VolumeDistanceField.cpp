/*============================================================================================================================================
                                                       VOLUMEDISTANCEFIELD.CPP
============================================================================================================================================*/
// 🧩 Triangle distance seeds, anisotropic separable EDT and three-axis parity for float volume bakes.
#include "Engine/ContentInterchange/VolumeDistanceField.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace Frontier
{
namespace
{
using Point = std::array<double, 3>;
Point Subtract(const Point &A, const Point &B) { return {A[0]-B[0], A[1]-B[1], A[2]-B[2]}; }
double Dot(const Point &A, const Point &B) { return A[0]*B[0]+A[1]*B[1]+A[2]*B[2]; }
Point Cross(const Point &A, const Point &B) { return {A[1]*B[2]-A[2]*B[1], A[2]*B[0]-A[0]*B[2], A[0]*B[1]-A[1]*B[0]}; }
struct Triangle { Point A, B, C; };

// Closest point on a triangle, matching the prototype's Voronoi-region tests.
double DistanceSquared(const Point &P, const Triangle &T)
{
    Point AB=Subtract(T.B,T.A), AC=Subtract(T.C,T.A), AP=Subtract(P,T.A);
    double D1=Dot(AB,AP), D2=Dot(AC,AP);
    if (D1<=0 && D2<=0) return Dot(AP,AP);
    Point BP=Subtract(P,T.B); double D3=Dot(AB,BP), D4=Dot(AC,BP);
    if (D3>=0 && D4<=D3) return Dot(BP,BP);
    double VC=D1*D4-D3*D2;
    if (VC<=0 && D1>=0 && D3<=0) { double V=D1/(D1-D3); Point Q{AP[0]-V*AB[0],AP[1]-V*AB[1],AP[2]-V*AB[2]}; return Dot(Q,Q); }
    Point CP=Subtract(P,T.C); double D5=Dot(AB,CP), D6=Dot(AC,CP);
    if (D6>=0 && D5<=D6) return Dot(CP,CP);
    double VB=D5*D2-D1*D6;
    if (VB<=0 && D2>=0 && D6<=0) { double W=D2/(D2-D6); Point Q{AP[0]-W*AC[0],AP[1]-W*AC[1],AP[2]-W*AC[2]}; return Dot(Q,Q); }
    double VA=D3*D6-D5*D4;
    if (VA<=0 && D4-D3>=0 && D5-D6>=0) { double W=(D4-D3)/((D4-D3)+(D5-D6)); Point BC=Subtract(T.C,T.B); Point Q{BP[0]-W*BC[0],BP[1]-W*BC[1],BP[2]-W*BC[2]}; return Dot(Q,Q); }
    Point N=Cross(AB,AC); double NN=Dot(N,N); if (NN<1e-30) return std::min({Dot(AP,AP),Dot(BP,BP),Dot(CP,CP)});
    double D=Dot(AP,N); return D*D/NN;
}

void TransformLine(const std::vector<double> &Input, std::vector<double> &Output, double StepSquared)
{
    const int Count=int(Input.size());
    std::vector<int> Sites(Count); std::vector<double> Limits(Count+1);
    int K=0; Sites[0]=0; Limits[0]=-std::numeric_limits<double>::infinity(); Limits[1]=std::numeric_limits<double>::infinity();
    for (int Q=1; Q<Count; ++Q)
    {
        if (Input[Q]>=1e18) continue;
        if (Input[Sites[K]]>=1e18) { Sites[0]=Q; continue; }
        double Boundary;
        for (;;)
        {
            int V=Sites[K]; Boundary=((Input[Q]+Q*Q*StepSquared)-(Input[V]+V*V*StepSquared))/(2*(Q-V)*StepSquared);
            if (Boundary>Limits[K]) break;
            --K;
        }
        Sites[++K]=Q; Limits[K]=Boundary; Limits[K+1]=std::numeric_limits<double>::infinity();
    }
    if (Input[Sites[0]]>=1e18) { std::fill(Output.begin(),Output.end(),1e18); return; }
    K=0;
    for (int Q=0; Q<Count; ++Q) { while (Limits[K+1]<Q) ++K; double D=Q-Sites[K]; Output[Q]=D*D*StepSquared+Input[Sites[K]]; }
}

// 🧩 Conservative bounds hierarchy accelerates parity rays while retaining the same triangle intersection test.
class AxisParityHierarchy
{
    struct Node
    {
        Point Lower{}, Upper{};
        uint32_t Begin = 0, Count = 0;
        int Left = -1, Right = -1;
    };
    const std::vector<Triangle> &Triangles;
    std::vector<uint32_t> Order;
    std::vector<Node> Nodes;
    int Build(uint32_t Begin, uint32_t Count)
    {
        Node N; N.Begin=Begin; N.Count=Count;
        N.Lower.fill(std::numeric_limits<double>::infinity()); N.Upper.fill(-std::numeric_limits<double>::infinity());
        for (uint32_t I=Begin; I<Begin+Count; ++I)
        {
            const auto &T=Triangles[Order[I]];
            for (int A=0; A<3; ++A) { N.Lower[A]=std::min({N.Lower[A],T.A[A],T.B[A],T.C[A]}); N.Upper[A]=std::max({N.Upper[A],T.A[A],T.B[A],T.C[A]}); }
        }
        const int Index=int(Nodes.size()); Nodes.push_back(N);
        if (Count > 8)
        {
            int Axis=0;
            for (int A=1; A<3; ++A) if (N.Upper[A]-N.Lower[A] > N.Upper[Axis]-N.Lower[Axis]) Axis=A;
            const uint32_t Middle=Begin+Count/2;
            std::nth_element(Order.begin()+Begin, Order.begin()+Middle, Order.begin()+Begin+Count, [&](uint32_t L, uint32_t R)
            { const auto &A=Triangles[L]; const auto &B=Triangles[R]; return A.A[Axis]+A.B[Axis]+A.C[Axis] < B.A[Axis]+B.B[Axis]+B.C[Axis]; });
            int Left=Build(Begin,Middle-Begin), Right=Build(Middle,Begin+Count-Middle);
            Nodes[Index].Left=Left; Nodes[Index].Right=Right;
        }
        return Index;
    }
public:
    explicit AxisParityHierarchy(const std::vector<Triangle> &Values) : Triangles(Values), Order(Values.size())
    { std::iota(Order.begin(),Order.end(),0); if (!Order.empty()) Build(0,uint32_t(Order.size())); }
    std::vector<double> Hits(int Axis, double First, double Second) const
    {
        Point Origin{}; Origin[Axis]=-1e5;
        if (Axis==0) { Origin[1]=First-2.11e-6; Origin[2]=Second+1.37e-6; }
        if (Axis==1) { Origin[0]=First+1.37e-6; Origin[2]=Second+1.37e-6; }
        if (Axis==2) { Origin[0]=First+1.37e-6; Origin[1]=Second-2.11e-6; }
        Point Direction{}; Direction[Axis]=1;
        std::vector<double> Hits; std::vector<int> Stack;
        if (!Nodes.empty()) Stack.push_back(0);
        while (!Stack.empty())
        {
            int Index=Stack.back(); Stack.pop_back(); const Node &N=Nodes[Index];
            bool Intersects=true;
            for (int A=0; A<3; ++A) if (A!=Axis && (Origin[A]<N.Lower[A]-1e-12 || Origin[A]>N.Upper[A]+1e-12)) Intersects=false;
            if (!Intersects) continue;
            if (N.Left>=0) { Stack.push_back(N.Left); Stack.push_back(N.Right); continue; }
            for (uint32_t I=N.Begin; I<N.Begin+N.Count; ++I)
            {
                const auto &T=Triangles[Order[I]];
                Point E1=Subtract(T.B,T.A), E2=Subtract(T.C,T.A), H=Cross(Direction,E2);
                double Det=Dot(E1,H); if (std::abs(Det)<1e-9) continue;
                Point S=Subtract(Origin,T.A); double U=Dot(S,H)/Det; if (U<0 || U>1) continue;
                Point Q=Cross(S,E1); double V=Dot(Direction,Q)/Det; if (V<0 || U+V>1) continue;
                Hits.push_back(-1e5+Dot(E2,Q)/Det);
            }
        }
        std::sort(Hits.begin(),Hits.end()); return Hits;
    }
};
bool Inside(double Coordinate, const std::vector<double> &Hits)
{
    return ((Hits.end()-std::upper_bound(Hits.begin(),Hits.end(),Coordinate+1e-7)) & 1) != 0;
}
}

void ExportVolumeDistanceFieldNrrd(const VolumeDistanceField &Field, const std::filesystem::path &Path)
{
    const auto D = Field.Dimensions;
    if (!D[0] || !D[1] || !D[2] || Field.Distances.size() != size_t(D[0])*D[1]*D[2])
        throw std::runtime_error("Volume data and dimensions do not agree.");
    std::ofstream File(Path, std::ios::binary | std::ios::trunc);
    if (!File) throw std::runtime_error("Could not create the volume file.");
    File << std::setprecision(9) << "NRRD0005\ntype: float\ndimension: 3\nsizes: " << D[0] << ' ' << D[1] << ' ' << D[2]
         << "\nencoding: raw\nendian: little\nspace dimension: 3\nspace directions: (" << Field.Step[0] << ",0,0) (0,"
         << Field.Step[1] << ",0) (0,0," << Field.Step[2] << ")\nspace origin: ("
         << Field.Minimum[0]+Field.Step[0]*.5f << ',' << Field.Minimum[1]+Field.Step[1]*.5f << ','
         << Field.Minimum[2]+Field.Step[2]*.5f << ")\n\n";
    if constexpr (std::endian::native == std::endian::little)
        File.write(reinterpret_cast<const char *>(Field.Distances.data()), std::streamsize(Field.Distances.size()*sizeof(float)));
    else
        for (float Value : Field.Distances)
        {
            uint32_t Bits = std::bit_cast<uint32_t>(Value);
            const char Bytes[]{char(Bits), char(Bits>>8), char(Bits>>16), char(Bits>>24)};
            File.write(Bytes, 4);
        }
    File.flush();
    if (!File) throw std::runtime_error("Could not write the complete volume file.");
}

VolumeDistanceField BakeVolumeDistanceField(const BakeMeshSet &Surface, std::array<uint32_t, 3> Dimensions,
    float Padding, bool Signed, float NarrowBand)
{
    for (auto &Dimension : Dimensions) Dimension=std::clamp(Dimension,8u,256u);
    VolumeDistanceField Field; Field.Dimensions=Dimensions; Field.Signed=Signed;
    std::vector<Triangle> Triangles;
    Point Minimum{1e30,1e30,1e30}, Maximum{-1e30,-1e30,-1e30};
    using VertexKey = std::tuple<long long,long long,long long>;
    std::map<VertexKey, uint32_t> Canonical;
    std::map<std::pair<uint32_t,uint32_t>, uint32_t> Edges;
    for (const auto &Part : Surface.Parts)
    {
        std::vector<Point> Positions; std::vector<uint32_t> VertexIds;
        for (const auto &Vertex : Part.Vertices)
        {
            Point P{}; for (int Axis=0; Axis<3; ++Axis) { P[Axis]=Vertex.Position[Axis]; Minimum[Axis]=std::min(Minimum[Axis],P[Axis]); Maximum[Axis]=std::max(Maximum[Axis],P[Axis]); }
            Positions.push_back(P);
            VertexKey Key{std::llround(P[0]*1e5),std::llround(P[1]*1e5),std::llround(P[2]*1e5)};
            auto Found=Canonical.find(Key); if (Found==Canonical.end()) Found=Canonical.emplace(Key,uint32_t(Canonical.size())).first;
            VertexIds.push_back(Found->second);
        }
        for (size_t Index=0; Index+2<Part.Indices.size(); Index+=3)
        {
            uint32_t A=Part.Indices[Index], B=Part.Indices[Index+1], C=Part.Indices[Index+2];
            if (A>=Positions.size() || B>=Positions.size() || C>=Positions.size()) throw std::runtime_error("Invalid triangle index");
            Triangles.push_back({Positions[A],Positions[B],Positions[C]});
            uint32_t Ids[]{VertexIds[A],VertexIds[B],VertexIds[C]};
            if (Ids[0]==Ids[1] || Ids[1]==Ids[2] || Ids[2]==Ids[0]) continue;
            for (int Edge=0; Edge<3; ++Edge) ++Edges[std::minmax(Ids[Edge],Ids[(Edge+1)%3])];
        }
    }
    if (Triangles.empty()) throw std::runtime_error("Volume bake requires triangles");
    Field.ClosedManifold=std::all_of(Edges.begin(),Edges.end(),[](const auto &Entry){ return Entry.second==2; });
    double Diagonal=std::sqrt(Dot(Subtract(Maximum,Minimum),Subtract(Maximum,Minimum)));
    if (!(Diagonal>0)) throw std::runtime_error("Volume source has empty bounds");
    Padding=std::clamp(Padding,.02f,.5f);
    for (int Axis=0; Axis<3; ++Axis) { Field.Minimum[Axis]=float(Minimum[Axis]-Diagonal*Padding); Field.Maximum[Axis]=float(Maximum[Axis]+Diagonal*Padding); Field.Step[Axis]=(Field.Maximum[Axis]-Field.Minimum[Axis])/Dimensions[Axis]; }
    const uint32_t NX=Dimensions[0], NY=Dimensions[1], NZ=Dimensions[2];
    const size_t Count=size_t(NX)*NY*NZ;
    auto Offset=[&](uint32_t X,uint32_t Y,uint32_t Z){ return (size_t(Z)*NY+Y)*NX+X; };
    std::vector<double> Distance(Count,1e20);
    for (const auto &T : Triangles)
    {
        uint32_t Low[3], High[3];
        for (int Axis=0; Axis<3; ++Axis)
        {
            double Lo=std::min({T.A[Axis],T.B[Axis],T.C[Axis]}), Hi=std::max({T.A[Axis],T.B[Axis],T.C[Axis]});
            Low[Axis]=uint32_t(std::clamp(int(std::floor((Lo-Field.Minimum[Axis])/Field.Step[Axis])),0,int(Dimensions[Axis])-1));
            High[Axis]=uint32_t(std::clamp(int(std::ceil((Hi-Field.Minimum[Axis])/Field.Step[Axis])),0,int(Dimensions[Axis])-1));
        }
        for (uint32_t Z=Low[2]; Z<=High[2]; ++Z) for (uint32_t Y=Low[1]; Y<=High[1]; ++Y) for (uint32_t X=Low[0]; X<=High[0]; ++X)
        {
            Point P{Field.Minimum[0]+(X+.5)*Field.Step[0],Field.Minimum[1]+(Y+.5)*Field.Step[1],Field.Minimum[2]+(Z+.5)*Field.Step[2]};
            size_t Slot=Offset(X,Y,Z); Distance[Slot]=std::min(Distance[Slot],DistanceSquared(P,T));
        }
    }
    for (int Axis=0; Axis<3; ++Axis)
    {
        uint32_t N=Dimensions[Axis]; std::vector<double> Input(N), Output(N);
        for (uint32_t Z=0; Z<(Axis==2?1:NZ); ++Z) for (uint32_t Y=0; Y<(Axis==1?1:NY); ++Y) for (uint32_t X=0; X<(Axis==0?1:NX); ++X)
        {
            for (uint32_t Q=0; Q<N; ++Q) Input[Q]=Distance[Offset(Axis==0?Q:X,Axis==1?Q:Y,Axis==2?Q:Z)];
            TransformLine(Input,Output,double(Field.Step[Axis])*Field.Step[Axis]);
            for (uint32_t Q=0; Q<N; ++Q) Distance[Offset(Axis==0?Q:X,Axis==1?Q:Y,Axis==2?Q:Z)]=Output[Q];
        }
    }
    std::vector<std::vector<double>> HitsX(size_t(NZ)*NY), HitsY(size_t(NZ)*NX), HitsZ(size_t(NY)*NX);
    if (Signed)
    {
        const AxisParityHierarchy Hierarchy(Triangles);
        for (uint32_t Z=0; Z<NZ; ++Z) for (uint32_t Y=0; Y<NY; ++Y) HitsX[size_t(Z)*NY+Y]=Hierarchy.Hits(0,Field.Minimum[1]+(Y+.5)*Field.Step[1],Field.Minimum[2]+(Z+.5)*Field.Step[2]);
        for (uint32_t Z=0; Z<NZ; ++Z) for (uint32_t X=0; X<NX; ++X) HitsY[size_t(Z)*NX+X]=Hierarchy.Hits(1,Field.Minimum[0]+(X+.5)*Field.Step[0],Field.Minimum[2]+(Z+.5)*Field.Step[2]);
        for (uint32_t Y=0; Y<NY; ++Y) for (uint32_t X=0; X<NX; ++X) HitsZ[size_t(Y)*NX+X]=Hierarchy.Hits(2,Field.Minimum[0]+(X+.5)*Field.Step[0],Field.Minimum[1]+(Y+.5)*Field.Step[1]);
    }
    Field.Distances.resize(Count); Field.Confidence.resize(Count,1.f);
    Field.MinimumDistance=std::numeric_limits<float>::infinity(); Field.MaximumDistance=-Field.MinimumDistance;
    for (uint32_t Z=0; Z<NZ; ++Z) for (uint32_t Y=0; Y<NY; ++Y) for (uint32_t X=0; X<NX; ++X)
    {
        size_t Slot=Offset(X,Y,Z); double D=std::sqrt(Distance[Slot]);
        if (Signed)
        {
            bool IX=Inside(Field.Minimum[0]+(X+.5)*Field.Step[0],HitsX[size_t(Z)*NY+Y]);
            bool IY=Inside(Field.Minimum[1]+(Y+.5)*Field.Step[1],HitsY[size_t(Z)*NX+X]);
            bool InsideField=IX==IY?IX:Inside(Field.Minimum[2]+(Z+.5)*Field.Step[2],HitsZ[size_t(Y)*NX+X]);
            if (IX!=IY) Field.Confidence[Slot]=.66f;
            if (InsideField) D=-D;
        }
        if (NarrowBand>0) D=std::clamp(D,-double(NarrowBand),double(NarrowBand));
        Field.Distances[Slot]=float(D); Field.MinimumDistance=std::min(Field.MinimumDistance,float(D)); Field.MaximumDistance=std::max(Field.MaximumDistance,float(D));
    }
    return Field;
}
}
