#include "WaterBodySequence.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace Frontier::ProjectZero {
void BuildPondGeometry(const ProjectFluid::PondWave& Pond,Vector3 Origin,GeometryStructure& Mesh) {
    if(!Mesh.QueryVertices().empty()||!Mesh.QueryIndices().empty())
        throw std::invalid_argument("Water mesh destination must be empty");
    if(!std::isfinite(Origin.x)||!std::isfinite(Origin.y)||!std::isfinite(Origin.z))
        throw std::invalid_argument("Water origin must be finite");
    const auto Columns=Pond.Columns(), Rows=Pond.Rows();
    std::vector<VertexRecord> Vertices(size_t(Columns)*Rows);
    for(uint32_t J=0;J<Rows;++J)for(uint32_t I=0;I<Columns;++I) {
        const float X=I*Pond.Dx()-Pond.Width()*.5f,Z=J*Pond.Dz()-Pond.Length()*.5f;
        const auto N=Pond.Normal(X,Z);
        auto& V=Vertices[size_t(J)*Columns+I];
        // Right-handed rotation from Flux Y-up to Frontier Z-up: (x,y,z)->(x,-z,y).
        V.SpatialLocation={Origin.x+X,Origin.y-Z,Origin.z+Pond.Sample(X,Z)};
        V.NormalDirection={N.x,-N.z,N.y};
        const Vector3 T=Vector3{N.y,0,-N.x}.Normalized();
        V.TangentDirection={T.x,T.y,T.z,1};
        V.TextureCoordinateU=float(I)/(Columns-1);V.TextureCoordinateV=float(J)/(Rows-1);
    }
    std::vector<uint32_t> Indices;
    Indices.reserve(size_t(Columns-1)*(Rows-1)*6);
    for(uint32_t J=0;J+1<Rows;++J)for(uint32_t I=0;I+1<Columns;++I) {
        // Respect the solver's solid masks rather than filling its rock holes with water.
        const float X=(I+.5f)*Pond.Dx()-Pond.Width()*.5f,Z=(J+.5f)*Pond.Dz()-Pond.Length()*.5f;
        if(!Pond.IsWet(X,Z))continue;
        const uint32_t A=J*Columns+I,B=A+1,D=A+Columns,C=D+1;
        for(auto Index:{A,C,B,A,D,C})Indices.push_back(Index);
    }
    Mesh.AppendVertices(Vertices.data(),Vertices.size());Mesh.AppendIndices(Indices.data(),Indices.size());
}
WaterBodyRegistration AppendPondSnapshot(SceneStructure& Level,const WaterBodySettings& S) {
    if(!std::isfinite(S.Disturbance)||std::abs(S.Disturbance)>2||
       !std::isfinite(S.SettleSeconds)||S.SettleSeconds<0||S.SettleSeconds>10)
        throw std::invalid_argument("Water disturbance/time out of bounds");
    ProjectFluid::PondWave Pond(S.Rows,S.Length,S.Width);
    Pond.Disturb(0,0,S.Disturbance,.4f);
    float Remaining=S.SettleSeconds;
    while(Remaining>0){const float Dt=std::min(Remaining,Pond.StableDt());Pond.Step(Dt);Remaining-=Dt;}
    GeometryStructure Mesh;BuildPondGeometry(Pond,S.Origin,Mesh);
    MaterialDescriptor Material;Material.Name="Water / Ripple snapshot";Material.Slabs.emplace_back();
    auto& Slab=Material.Slabs.front();
    Slab.BaseWeight=0;Slab.SpecularIor=1.333f;Slab.SpecularRoughness=.035f;
    Slab.TransmissionWeight=1;Slab.TransmissionColor[0]=.82f;Slab.TransmissionColor[1]=.95f;Slab.TransmissionColor[2]=.98f;
    // Open height surface: explicitly a thin sheet, NOT a closed refractive water volume.
    Slab.GeometryThinWalled=true;
    const auto Mat=Level.RegisterMaterial(Material);
    const auto First=static_cast<uint32_t>(Level.QueryInstances().size());
    const Matrix4x4 Identity;
    (void)Level.RegisterInstance(Mesh,Identity,Mat,InstanceFlagDoubleSided);
    const auto Count=static_cast<uint32_t>(Level.QueryInstances().size())-First;
    const auto Placement=Level.RegisterPlacement("Water Body / Ripple snapshot",0xffffffffu,Identity,Identity);
    Level.AttachInstances(Placement,First,Count);
    return {Placement,First,Count,Mat};
}
}
