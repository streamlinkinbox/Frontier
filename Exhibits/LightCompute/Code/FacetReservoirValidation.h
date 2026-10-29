/*============================================================================================================================================
                                                    FACETRESERVOIRVALIDATION.H
============================================================================================================================================*/
// 🧩 Compare RGB descriptor layout, facet interpolation, revision rejection and current geometry against doubles.
#pragma once
#include "FacetReservoirDescriptor.h"

namespace
{
void ValidateFacetReservoir(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Count = 4096;
    auto Scene = TriangleReconnectionScene(0);
    std::vector<std::array<SpatialVector, 3>> Triangles;
    for (uint32_t I = 0; I < 8; ++I)
    {
        const double Angle = I * .7;
        SpatialVector A{2 * std::cos(Angle), 2 * std::sin(Angle), I * .2};
        Triangles.push_back({A, {A.X + .7, A.Y + .13, A.Z + .2 * std::cos(I)},
                               {A.X - .1, A.Y + .6, A.Z + .3 * std::sin(I)}});
    }
    TriangleVisibilitySpace Space;
    Space.Build(Triangles);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.clear();
    for (const auto& T : Scene.Visibility.Corners)
    {
        LightComputeFacet F{};
        F.A = T.A; F.B = T.B; F.C = T.C;
        auto Normalize = [](std::array<double, 3> P)
        {
            const double L = std::sqrt(P[0]*P[0]+P[1]*P[1]+P[2]*P[2]);
            return std::array<float, 4>{float(P[0]/L),float(P[1]/L),float(P[2]/L),0};
        };
        std::array<double, 3> B{}, C{};
        for (uint32_t K = 0; K < 3; ++K) { B[K]=F.B[K]-F.A[K]; C[K]=F.C[K]-F.A[K]; }
        const std::array<double, 3> N{B[1]*C[2]-B[2]*C[1],B[2]*C[0]-B[0]*C[2],B[0]*C[1]-B[1]*C[0]};
        F.NormalA=Normalize(N); F.NormalB=Normalize({N[0]+.07,N[1]-.03,N[2]});
        F.NormalC=Normalize({N[0]-.04,N[1]+.05,N[2]});
        F.AlbedoRoughness={.4f,.5f,.6f,.7f};
        Scene.Surfaces.push_back(F);
    }
    Scene.Width=64; Scene.Height=64; Scene.Receivers.assign(Count, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::filesystem::create_directories(Directory);
    std::printf("FACET_RESERVOIR_DEVICE name=%s type=%u descriptor_bytes=%zu quartet_bytes=%zu\n",
                Compute.Properties.deviceName,unsigned(Compute.Properties.deviceType),sizeof(FacetReservoirDescriptor),
                4*sizeof(FacetReservoirDescriptor)+16);
    const std::array<std::array<float, 3>, 4> Receivers{{{0,0,4},{0,0,-3},{4,-2,1},{-3,3,2}}};
    for (uint32_t Phase=0; Phase<6; ++Phase)
    {
        if (Phase==4) for (auto& F : Scene.Surfaces) { F.A[0]+=.125f; F.B[0]+=.125f; F.C[0]+=.125f; }
        const uint32_t CurrentRevision=Phase>=4?8:7, BirthRevision=Phase==5?8:7;
        std::vector<FacetReservoirDescriptor> Descriptors(Count);
        for (uint32_t I=0; I<Count; ++I)
        {
            auto& D=Descriptors[I];
            D={I%8,BirthRevision,float((I*17)%1024)/2048,float((I*37)%1024)/2048,
                {float(1+I%7)/8,float(1+I%5)/6,float(1+I%3)/4},float(1+I%11)/8};
            const uint32_t Category=I%32;
            if (Category==0) D={UINT32_MAX,0,0,0,{0,0,0},0};
            if (Category==1) D.FacetIndex=UINT32_MAX;
            if (Category==2) D.Revision=BirthRevision-1;
            if (Category==3) D.U=std::numeric_limits<float>::quiet_NaN();
            if (Category==4) D.V=std::numeric_limits<float>::infinity();
            if (Category==5) D.Outgoing[1]=-1;
            if (Category==6) D.Weight=-1;
            if (Category==7) D.U=-.1f;
            if (Category==8) D.V=1.1f;
            if (Category==9) { D.U=.75f; D.V=.5f; }
            if (Category==10) D.Outgoing[2]=std::numeric_limits<float>::quiet_NaN();
            if (Category==11) D.Weight=std::numeric_limits<float>::infinity();
            if (Category==12) D.Weight=0;
            if (Category==13) D.Revision=UINT32_MAX;
            if (Category==14) { D.U=float((I/32)%3==1); D.V=float((I/32)%3==2); }
            auto& R=Scene.Receivers[I];
            static_assert(sizeof(D)==32);
            std::memcpy(R.PositionHit.data(), &D, 16);
            std::memcpy(R.NormalRoughness.data(), reinterpret_cast<const char*>(&D)+16,16);
            R.ViewMetallic={Receivers[Phase%4][0],Receivers[Phase%4][1],Receivers[Phase%4][2],float(CurrentRevision)};
        }
        const auto Image=Compute.ExecuteDiffuse(Scene,2,17,true,false,true);
        const auto* Storage=static_cast<const std::array<float,4>*>(Compute.Buffers[6].Address);
        uint32_t Valid=0,Empty=0,Rejected=0; double MaximumPosition=0,MaximumNormal=0,MaximumEstimate=0;
        for (uint32_t I=0; I<Count; ++I)
        {
            const auto& D=Descriptors[I]; const auto& R=Scene.Receivers[I];
            Require(std::memcmp(&D,Storage[4*Count+I].data(),16)==0 &&
                    std::memcmp(reinterpret_cast<const char*>(&D)+16,Storage[5*Count+I].data(),16)==0,
                    "Facet descriptor did not roundtrip its actual float32/uint32 bytes");
            const uint32_t Expected=I%32==0?0:((I%32>=14 && Phase!=4)?1:2);
            Require(Image[I][3]==float(Expected),"Descriptor rejection or revision barrier failed");
            if (Expected!=1)
            {
                Empty+=Expected==0; Rejected+=Expected==2;
                for (uint32_t K=0; K<3; ++K) Require(Image[I][K]==0,"Rejected descriptor contributed radiance");
                continue;
            }
            ++Valid;
            const auto& F=Scene.Surfaces[D.FacetIndex];
            std::array<double,3> P{},N{},Delta{}; double Squared=0,NormalSquared=0;
            for (uint32_t K=0; K<3; ++K)
            {
                P[K]=F.A[K]*(1-double(D.U)-D.V)+F.B[K]*double(D.U)+F.C[K]*double(D.V);
                N[K]=F.NormalA[K]*(1-double(D.U)-D.V)+F.NormalB[K]*double(D.U)+F.NormalC[K]*double(D.V);
                NormalSquared+=N[K]*N[K]; Delta[K]=R.ViewMetallic[K]-P[K]; Squared+=Delta[K]*Delta[K];
            }
            double Dot=0;
            for (uint32_t K=0; K<3; ++K) { N[K]/=std::sqrt(NormalSquared); Dot+=N[K]*Delta[K]/std::sqrt(Squared); }
            const double Geometry=std::max(0.,Dot)/Squared;
            for (uint32_t K=0; K<3; ++K)
            {
                MaximumPosition=std::max(MaximumPosition,std::abs(P[K]-Storage[Count+I][K]));
                MaximumNormal=std::max(MaximumNormal,std::abs(N[K]-Storage[2*Count+I][K]));
                MaximumEstimate=std::max(MaximumEstimate,std::abs(D.Outgoing[K]*D.Weight*Geometry-Image[I][K]));
            }
            Require(std::abs(Storage[3*Count+I][0]-(1./64+Geometry))<2e-5,"Positive support floor differs");
        }
        Require(MaximumPosition<2e-5 && MaximumNormal<2e-5 && MaximumEstimate<2e-5,"Facet reconstruction error");
        std::printf("FACET_RESERVOIR_PHASE phase=%u valid=%u empty=%u rejected=%u position_error=%.12g normal_error=%.12g estimate_error=%.12g\n",
                    Phase,Valid,Empty,Rejected,MaximumPosition,MaximumNormal,MaximumEstimate);
        std::ofstream Capture(Directory+"/FacetReservoir"+std::to_string(Phase)+".f32",std::ios::binary);
        Capture.exceptions(std::ios::failbit|std::ios::badbit);
        Capture.write(reinterpret_cast<const char*>(Storage),6*Count*16);
        std::fflush(stdout);
    }
    std::printf("FACET_RESERVOIR_DECISION descriptor=1 production_promote=0 renderer_quality=0\n");
}
}
