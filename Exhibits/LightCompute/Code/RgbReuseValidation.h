/*============================================================================================================================================
                                                        RGBREUSEVALIDATION.H
============================================================================================================================================*/
// 🧩 Replay persistent RGB descriptors independently and prove retained bytes survive receiver motion and resets.
#pragma once

namespace
{
void ValidateRgbReuse(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Samples = 1024, Methods = 4, Count = Samples*Methods;
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
    const auto Original = Scene.Surfaces;
    Scene.Width = 64; Scene.Height = 64;
    Scene.Receivers.assign(Count, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::filesystem::create_directories(Directory);
    const std::array<std::array<float,3>,4> Corners{{{0,0,4},{0,0,-3},{4,-2,1},{-3,3,2}}};
    uint64_t RetainedChecks = 0;
    uint32_t Ambiguous = 0;
    std::printf("RGB_REUSE_DEVICE name=%s type=%u samples=1024 methods=4 steps=32 fixtures=3\n",
                Compute.Properties.deviceName,unsigned(Compute.Properties.deviceType));
    for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
    {
        Scene.Surfaces = Original;
        std::vector<std::array<FacetReservoirDescriptor,4>> Shadow(Count), Previous(Count);
        std::vector<std::array<double,4>> Weights(Count);
        for (uint32_t Step = 0; Step < 32; ++Step)
        {
            if (Step == 24)
            {
                for (auto& Facet : Scene.Surfaces)
                {
                    Facet.A[0]+=.125f; Facet.B[0]+=.125f; Facet.C[0]+=.125f;
                }
            }
            const uint32_t Epoch = Step>=24?9:Step>=16?8:7;
            const bool Reset = Step==0 || Step==16 || Step==24;
            const float Scale = Step>=16?2.f:1.f;
            std::array<float,3> Receiver = Corners[0];
            if (Fixture==1)
            {
                const double Blend=.5*(1-std::cos(2*3.141592653589793*Step/16));
                Receiver={float(4*Blend),float(-2*Blend),float(4-3*Blend)};
            }
            if (Fixture==2) Receiver=Corners[(Step/4)%4];
            std::array<double,8> Geometry{};
            for (uint32_t FacetIndex=0;FacetIndex<8;++FacetIndex)
            {
                const auto& Facet=Scene.Surfaces[FacetIndex];
                std::array<double,3> Normal{},Delta{};
                double NormalSquared=0,DistanceSquared=0;
                for (uint32_t Axis=0;Axis<3;++Axis)
                {
                    const double U=float(.2),V=float(.3);
                    const double Point=Facet.A[Axis]*(1-U-V)+Facet.B[Axis]*U+Facet.C[Axis]*V;
                    Normal[Axis]=Facet.NormalA[Axis]*(1-U-V)+Facet.NormalB[Axis]*U+Facet.NormalC[Axis]*V;
                    NormalSquared+=Normal[Axis]*Normal[Axis];
                    Delta[Axis]=Receiver[Axis]-Point; DistanceSquared+=Delta[Axis]*Delta[Axis];
                }
                double Dot=0;
                for (uint32_t Axis=0;Axis<3;++Axis) Dot+=Normal[Axis]*Delta[Axis]/std::sqrt(NormalSquared*DistanceSquared);
                Geometry[FacetIndex]=std::max(0.,Dot)/DistanceSquared;
            }
            auto Rgb = [Scale](uint32_t FacetIndex)
            {
                return std::array<float,3>{float(1+FacetIndex%7)/8*Scale,float(1+FacetIndex%5)/6*Scale,
                                          float(1+FacetIndex%3)/4*Scale};
            };
            for (uint32_t Pixel=0;Pixel<Count;++Pixel)
            {
                const uint32_t Seed=RgbQuartetHash((Pixel%Samples+0x12345678u)^(Step*0x6c8e9cf5u));
                const std::array<uint32_t,4> First{Seed,Pixel/Samples,Step,Fixture};
                const std::array<uint32_t,2> Second{Epoch,uint32_t(Step==0)};
                std::memcpy(Scene.Receivers[Pixel].PositionHit.data(),First.data(),16);
                std::memcpy(Scene.Receivers[Pixel].NormalRoughness.data(),Second.data(),8);
                Scene.Receivers[Pixel].NormalRoughness[2]=Scale;
                Scene.Receivers[Pixel].ViewMetallic={Receiver[0],Receiver[1],Receiver[2],0};
            }
            Compute.ExecuteDiffuse(Scene,2,17,true,false,Step==0);
            const auto* Storage=static_cast<const std::array<float,4>*>(Compute.Buffers[6].Address);
            double MaximumEstimate=0,MaximumWeight=0;
            std::array<uint64_t,4> DrawTotals{},KeptTotals{},EvaluationTotals{};
            for (uint32_t Pixel=0;Pixel<Count;++Pixel)
            {
                const uint32_t Method=Pixel/Samples;
                const uint32_t Seed=RgbQuartetHash((Pixel%Samples+0x12345678u)^(Step*0x6c8e9cf5u));
                std::array<double,3> Estimate{};
                uint32_t Draws=0,Kept=0,Evaluations=0;
                for (uint32_t Lane=0;Lane<4;++Lane)
                {
                    const bool Refresh=Reset || Method==1 || Lane/2==Step%2;
                    const bool Generate=Method<2?Refresh:Method==3 || Refresh;
                    FacetReservoirDescriptor Actual{};
                    std::memcpy(&Actual,Storage[(2+2*Lane)*Count+Pixel].data(),16);
                    std::memcpy(reinterpret_cast<char*>(&Actual)+16,Storage[(3+2*Lane)*Count+Pixel].data(),16);
                    if (Method==0 && !Refresh)
                    {
                        ++Kept; ++RetainedChecks;
                        Require(std::memcmp(&Actual,&Previous[Pixel][Lane],32)==0,"Retained RGB descriptor bytes changed");
                    }
                    else
                    {
                        Shadow[Pixel][Lane]={UINT32_MAX,0,0,0,{0,0,0},0}; Weights[Pixel][Lane]=0;
                        if (Generate)
                        {
                            Draws+=2;
                            const uint32_t First=RgbQuartetHash(Seed^((3*Lane+1)*0x9e3779b9u))%9;
                            const uint32_t Second=RgbQuartetHash(Seed^((3*Lane+2)*0x9e3779b9u))%9;
                            if (Method>=2)
                            {
                                for (uint32_t Candidate : {First,Second})
                                {
                                    if (Candidate>=8) continue;
                                    ++Evaluations;
                                    const auto Outgoing=Rgb(Candidate);
                                    for (uint32_t Channel=0;Channel<3;++Channel)
                                        Estimate[Channel]+=Outgoing[Channel]*Geometry[Candidate];
                                }
                            }
                            else
                            {
                                const double FirstTarget=First<8?1./64+Geometry[First]:0;
                                const double SecondTarget=Second<8?1./64+Geometry[Second]:0;
                                const double Sum=FirstTarget+SecondTarget;
                                if (Sum>0)
                                {
                                    const double Uniform=double(RgbQuartetHash(Seed^((3*Lane+3)*0x9e3779b9u))>>8)/16777216;
                                    uint32_t Selected=Uniform<FirstTarget/Sum?First:Second;
                                    if (Actual.FacetIndex!=Selected)
                                    {
                                        Require(std::abs(Uniform-FirstTarget/Sum)<2e-6,"Persistent RGB selector disagrees");
                                        ++Ambiguous; Selected=Actual.FacetIndex;
                                    }
                                    Require(Selected<8 && (Selected==First || Selected==Second),"Foreign persistent proposal");
                                    Weights[Pixel][Lane]=9*Sum/(2*(1./64+Geometry[Selected]));
                                    Shadow[Pixel][Lane]={Selected,Epoch,.2f,.3f,Rgb(Selected),float(Weights[Pixel][Lane])};
                                }
                            }
                        }
                    }
                    const auto& Expected=Shadow[Pixel][Lane];
                    if (Expected.FacetIndex==UINT32_MAX)
                        Require(std::memcmp(&Actual,&Expected,32)==0,"Noncanonical persistent empty descriptor");
                    else
                    {
                        Require(Actual.FacetIndex==Expected.FacetIndex && Actual.Revision==Expected.Revision &&
                                Actual.Revision==Epoch && Actual.U==.2f && Actual.V==.3f,"Stale persistent RGB identity");
                        const double Error=std::abs(Actual.Weight-Weights[Pixel][Lane])/std::max(1.,Weights[Pixel][Lane]);
                        Require(std::isfinite(Actual.Weight) && Error<2e-5,"Persistent birth weight changed");
                        MaximumWeight=std::max(MaximumWeight,Error);
                        for (uint32_t Channel=0;Channel<3;++Channel)
                        {
                            Require(std::isfinite(Actual.Outgoing[Channel]) &&
                                    std::abs(Actual.Outgoing[Channel]-Expected.Outgoing[Channel])<2e-5,"Stale persistent RGB suffix");
                            Estimate[Channel]+=Expected.Outgoing[Channel]*Weights[Pixel][Lane]*Geometry[Expected.FacetIndex]/4;
                        }
                        ++Evaluations;
                    }
                    Previous[Pixel][Lane]=Actual;
                }
                for (uint32_t Channel=0;Channel<3;++Channel)
                {
                    if (Method>=2) Estimate[Channel]*=9./Draws;
                    const double Error=std::abs(Storage[Pixel][Channel]-Estimate[Channel])/std::max(1.,Estimate[Channel]);
                    Require(std::isfinite(Storage[Pixel][Channel]) && Error<2e-5,"Persistent RGB estimate differs from CPU");
                    MaximumEstimate=std::max(MaximumEstimate,Error);
                }
                Require(Storage[Count+Pixel][0]==float(Draws) && Storage[Count+Pixel][1]==float(Kept) &&
                        Storage[Count+Pixel][2]==float(Evaluations) && Storage[Count+Pixel][3]==float(Reset),
                        "Persistent work/reset telemetry differs");
                DrawTotals[Method]+=Draws; KeptTotals[Method]+=Kept; EvaluationTotals[Method]+=Evaluations;
            }
            std::printf("RGB_REUSE_CASE fixture=%u step=%u reset=%u estimate_error=%.12g weight_error=%.12g\n",
                        Fixture,Step,unsigned(Reset),MaximumEstimate,MaximumWeight);
            for (uint32_t Method=0;Method<4;++Method)
                std::printf("RGB_REUSE_WORK fixture=%u step=%u method=%u draws=%llu kept=%llu evaluations=%llu\n",
                            Fixture,Step,Method,(unsigned long long)DrawTotals[Method],(unsigned long long)KeptTotals[Method],
                            (unsigned long long)EvaluationTotals[Method]);
            std::printf("RGB_REUSE_EXACT fixture=%u step=%u",Fixture,Step);
            for (uint32_t Channel=0;Channel<3;++Channel)
            {
                double Exact=0;
                for (uint32_t FacetIndex=0;FacetIndex<8;++FacetIndex) Exact+=Rgb(FacetIndex)[Channel]*Geometry[FacetIndex];
                std::printf(" channel%u=%.17g",Channel,Exact);
            }
            std::printf("\n");
            std::ofstream Capture(Directory+"/RgbReuse"+std::to_string(Fixture)+"-"+std::to_string(Step)+".f32",std::ios::binary);
            Capture.exceptions(std::ios::failbit|std::ios::badbit);
            Capture.write(reinterpret_cast<const char*>(Storage),19*Count*16);
            std::fflush(stdout);
        }
    }
    Require(Ambiguous<=16 && RetainedChecks==178176,"Persistent ambiguity or retention count differs");
    std::printf("RGB_REUSE_DECISION correctness=1 retained_checks=%llu ambiguous=%u production_promote=0\n",
                (unsigned long long)RetainedChecks,Ambiguous);
}
}
