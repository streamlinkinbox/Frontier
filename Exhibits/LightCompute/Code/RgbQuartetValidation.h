/*============================================================================================================================================
                                                       RGBQUARTETVALIDATION.H
============================================================================================================================================*/
// 🧩 Independently replay RGB quartet selection,descriptor transport and receiver evaluation in double precision.
#pragma once
#include "FacetReservoirDescriptor.h"

namespace
{
uint32_t RgbQuartetHash(uint32_t Bits)
{
    Bits ^= Bits >> 16; Bits *= 0x7feb352du;
    Bits ^= Bits >> 15; Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}

void ValidateRgbQuartet(const char* Driver, const std::string& Shaders, const std::string& Directory)
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
    Scene.Width = 64; Scene.Height = 64;
    Scene.Receivers.assign(Count, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::filesystem::create_directories(Directory);
    const std::array<std::array<double, 3>, 4> Positions{{{0,0,4},{0,0,-3},{4,-2,1},{-3,3,2}}};
    std::array<std::array<double, 8>, 4> Geometry{};
    for (uint32_t ReceiverIndex = 0; ReceiverIndex < 4; ++ReceiverIndex)
    {
        for (uint32_t FacetIndex = 0; FacetIndex < 8; ++FacetIndex)
        {
            const auto& Corners = Scene.Surfaces[FacetIndex];
            std::array<double, 3> Normal{}, Delta{};
            double NormalSquared = 0, DistanceSquared = 0;
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                const double U = float(.2), V = float(.3);
                const double Point = Corners.A[Axis]*(1-U-V)+Corners.B[Axis]*U+Corners.C[Axis]*V;
                Normal[Axis] = Corners.NormalA[Axis]*(1-U-V)+Corners.NormalB[Axis]*U+Corners.NormalC[Axis]*V;
                NormalSquared += Normal[Axis]*Normal[Axis];
                Delta[Axis] = Positions[ReceiverIndex][Axis]-Point;
                DistanceSquared += Delta[Axis]*Delta[Axis];
            }
            double Dot = 0;
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
                Dot += Normal[Axis]*Delta[Axis]/std::sqrt(NormalSquared*DistanceSquared);
            Geometry[ReceiverIndex][FacetIndex] = std::max(0., Dot)/DistanceSquared;
        }
    }
    auto Rgb = [](uint32_t FacetIndex)
    {
        return std::array<double,3>{float(1+FacetIndex%7)/8,float(1+FacetIndex%5)/6,float(1+FacetIndex%3)/4};
    };
    std::printf("RGB_QUARTET_DEVICE name=%s type=%u descriptor_bytes=32 quartet_bytes=144 raw_bytes=272\n",
                Compute.Properties.deviceName, unsigned(Compute.Properties.deviceType));
    uint32_t Ambiguous = 0;
    for (uint32_t Scenario = 0; Scenario < 20; ++Scenario)
    {
        const uint32_t BirthIndex = Scenario < 16 ? Scenario/4 : 1;
        const uint32_t CurrentIndex = Scenario < 16 ? Scenario%4 : 0;
        const uint32_t BirthRevision = Scenario == 17 ? 8 : 7;
        const uint32_t CurrentRevision = Scenario == 16 || Scenario == 17 ? 8 : 7;
        for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
        {
            const uint32_t Seed = RgbQuartetHash(Pixel+0x12345678u);
            const std::array<uint32_t,4> First{Seed,Scenario,BirthIndex,CurrentIndex};
            const std::array<uint32_t,4> Second{BirthRevision,CurrentRevision,0,0};
            std::memcpy(Scene.Receivers[Pixel].PositionHit.data(), First.data(), 16);
            std::memcpy(Scene.Receivers[Pixel].NormalRoughness.data(), Second.data(), 16);
        }
        Compute.ExecuteDiffuse(Scene, 2, 17, true, false, true);
        const auto* Storage = static_cast<const std::array<float,4>*>(Compute.Buffers[6].Address);
        double MaximumEstimate = 0, MaximumWeight = 0, MaximumSuffix = 0;
        uint32_t NullTotal = 0, EmptyTotal = 0;
        for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
        {
            const uint32_t Seed = RgbQuartetHash(Pixel+0x12345678u);
            std::array<double,3> Estimate{}, Raw{};
            uint32_t Nulls = 0, Empty = 0;
            for (uint32_t ReservoirIndex = 0; ReservoirIndex < 4; ++ReservoirIndex)
            {
                uint32_t First = RgbQuartetHash(Seed^((3*ReservoirIndex+1)*0x9e3779b9u))%9;
                uint32_t Second = RgbQuartetHash(Seed^((3*ReservoirIndex+2)*0x9e3779b9u))%9;
                if (Scenario == 18) { First = 8; Second = 8; }
                if (Scenario == 19) { First = ReservoirIndex == 0 ? 0 : 8; Second = 8; }
                const double Uniform = double(RgbQuartetHash(Seed^((3*ReservoirIndex+3)*0x9e3779b9u))>>8)/16777216;
                const double FirstTarget = First<8 ? 1./64+Geometry[BirthIndex][First] : 0;
                const double SecondTarget = Second<8 ? 1./64+Geometry[BirthIndex][Second] : 0;
                const double Sum = 9*(FirstTarget+SecondTarget);
                const uint32_t Expected = Sum>0 && Uniform<FirstTarget/(FirstTarget+SecondTarget) ? First : Second;
                FacetReservoirDescriptor Descriptor{};
                std::memcpy(&Descriptor, Storage[(2+2*ReservoirIndex)*Count+Pixel].data(), 16);
                std::memcpy(reinterpret_cast<char*>(&Descriptor)+16, Storage[(3+2*ReservoirIndex)*Count+Pixel].data(), 16);
                Nulls += uint32_t(First==8)+uint32_t(Second==8);
                if (Sum == 0)
                {
                    ++Empty;
                    const FacetReservoirDescriptor Canonical{UINT32_MAX,0,0,0,{0,0,0},0};
                    Require(std::memcmp(&Descriptor,&Canonical,32)==0, "Noncanonical empty RGB reservoir");
                    continue;
                }
                const uint32_t Selected = Descriptor.FacetIndex;
                Require(Selected == First || Selected == Second, "RGB selector chose a foreign proposal");
                Require(Selected < 8, "RGB selector chose a null proposal");
                if (Selected != Expected)
                {
                    Require(std::abs(Uniform-FirstTarget/(FirstTarget+SecondTarget))<2e-6,
                            "RGB selector identity disagrees away from rounding boundary");
                    ++Ambiguous;
                }
                Require(Descriptor.Revision==BirthRevision && Descriptor.U==.2f && Descriptor.V==.3f,
                        "RGB descriptor identity or barycentrics changed in transport");
                const double Weight = Sum/(2*(1./64+Geometry[BirthIndex][Selected]));
                const double WeightError = std::abs(Descriptor.Weight-Weight)/std::max(1.,Weight);
                Require(std::isfinite(Descriptor.Weight) && WeightError<2e-5,"RGB birth normalizer differs");
                MaximumWeight = std::max(MaximumWeight,WeightError);
                const auto Outgoing = Rgb(Selected);
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                {
                    Require(std::isfinite(Descriptor.Outgoing[Channel]) &&
                            std::abs(Descriptor.Outgoing[Channel]-Outgoing[Channel])<2e-5,
                            "Computed RGB suffix differs beyond declared precision");
                    MaximumSuffix=std::max(MaximumSuffix,std::abs(Descriptor.Outgoing[Channel]-Outgoing[Channel]));
                    if (BirthRevision==CurrentRevision)
                        Estimate[Channel] += Outgoing[Channel]*Weight*Geometry[CurrentIndex][Selected]/4;
                }
                // Raw proposals are compared separately even when the selected descriptor is null.
            }
            for (uint32_t DrawIndex = 0; DrawIndex < 8; ++DrawIndex)
            {
                const uint32_t ReservoirIndex = DrawIndex/2;
                uint32_t Candidate = RgbQuartetHash(Seed^((3*ReservoirIndex+1+DrawIndex%2)*0x9e3779b9u))%9;
                if (Scenario==18) Candidate=8;
                if (Scenario==19) Candidate=DrawIndex==0?0:8;
                if (Candidate<8 && BirthRevision==CurrentRevision)
                {
                    const auto Outgoing=Rgb(Candidate);
                    for (uint32_t Channel=0;Channel<3;++Channel)
                        Raw[Channel]+=Outgoing[Channel]*Geometry[CurrentIndex][Candidate]*9/8;
                }
            }
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                const double Error=std::abs(Storage[Pixel][Channel]-Estimate[Channel])/std::max(1.,Estimate[Channel]);
                const double RawError=std::abs(Storage[Count+Pixel][Channel]-Raw[Channel])/std::max(1.,Raw[Channel]);
                Require(std::isfinite(Storage[Pixel][Channel]) && Error<2e-5,"RGB quartet estimate differs");
                Require(std::isfinite(Storage[Count+Pixel][Channel]) && RawError<2e-5,"Raw RGB reference differs");
                MaximumEstimate=std::max({MaximumEstimate,Error,RawError});
            }
            Require(Storage[13*Count+Pixel][0]==float(Nulls) && Storage[13*Count+Pixel][1]==float(Empty),
                    "Null RGB proposals not counted");
            NullTotal+=Nulls; EmptyTotal+=Empty;
        }
        std::printf("RGB_QUARTET_CASE scenario=%u birth=%u current=%u nulls=%u empty=%u estimate_error=%.12g weight_error=%.12g suffix_error=%.12g\n",
                    Scenario,BirthIndex,CurrentIndex,NullTotal,EmptyTotal,MaximumEstimate,MaximumWeight,MaximumSuffix);
        std::ofstream Capture(Directory+"/RgbQuartet"+std::to_string(Scenario)+".f32",std::ios::binary);
        Capture.exceptions(std::ios::failbit|std::ios::badbit);
        Capture.write(reinterpret_cast<const char*>(Storage),14*Count*16);
        if (Scenario<16)
        {
            std::printf("RGB_QUARTET_EXACT scenario=%u",Scenario);
            for (uint32_t Channel=0;Channel<3;++Channel)
            {
                double Exact=0;
                for (uint32_t FacetIndex=0;FacetIndex<8;++FacetIndex)
                    Exact+=Rgb(FacetIndex)[Channel]*Geometry[CurrentIndex][FacetIndex];
                std::printf(" channel%u=%.17g",Channel,Exact);
            }
            std::printf("\n");
        }
        std::fflush(stdout);
    }
    Require(Ambiguous<=16,"Too many RGB selection ambiguities");
    std::printf("RGB_QUARTET_DECISION selector=1 ambiguous=%u production_promote=0 renderer_quality=0\n",Ambiguous);
}
}
