/*============================================================================================================================================
                                                        QUERYCALIBRATIONVALIDATION.H
============================================================================================================================================*/
// 🧩 Calibrate fresh counts on separate pilot seeds and validate held-out query budgets and RGB estimates.
#pragma once

namespace
{
void ValidateQueryCalibration(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr bool Elide = true;
    const char* Label = "QUERY_CALIBRATION";
    constexpr uint32_t Samples = 1024, Methods = 4, Count = Samples*Methods;
    auto Scene = RgbTraceScene(0);
    Scene.Width = 64; Scene.Height = 64;
    Scene.Receivers.assign(Count, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::filesystem::create_directories(Directory);
    const std::array<std::array<float,3>,4> Corners{{{0,0,4},{0,0,-3},{4,-2,1},{-3,3,2}}};
    uint64_t RetainedChecks = 0;
    uint32_t Ambiguous = 0;
    std::printf("%s_DEVICE name=%s type=%u samples=1024 methods=4 steps=32 fixtures=3\n",
                Label,Compute.Properties.deviceName,unsigned(Compute.Properties.deviceType));
    for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
    {
        Scene = RgbTraceScene(Fixture);
        std::vector<std::array<FacetReservoirDescriptor,4>> Shadow(Count), Previous(Count);
        std::vector<std::array<double,4>> Weights(Count);
        std::array<std::array<uint32_t,4>,4096> PilotDescriptors{};
        for (uint32_t Step = 0; Step < 32; ++Step)
        {
            if (Step==24) Scene=RgbTraceScene((Fixture+1)%3);
            if (Step==16 || Step==24)
                for (auto& Facet:Scene.Surfaces) Facet.AlbedoRoughness[0]*=.5f;
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
            std::array<bool,8> Visibility{};
            std::array<std::array<float,3>,8> RgbTable{};
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
                TraceVector Point{},Origin{},DeltaVector{};
                const auto UnitNormal=TraceNormalize(Normal);
                for (uint32_t Axis=0;Axis<3;++Axis)
                {
                    const double U=float(.2),V=float(.3);
                    Point[Axis]=Facet.A[Axis]*(1-U-V)+Facet.B[Axis]*U+Facet.C[Axis]*V;
                    Origin[Axis]=Point[Axis]+UnitNormal[Axis]*.002f;
                    DeltaVector[Axis]=Receiver[Axis]-Point[Axis];
                }
                Visibility[FacetIndex]=!TraceBlocked(Scene,Origin,TraceNormalize(DeltaVector),std::sqrt(DistanceSquared)-.004f);
                const TraceVector Light{double(.8f),0,double(.6f)};
                const bool Lit=!TraceBlocked(Scene,Origin,Light,100);
                for (uint32_t Channel=0;Channel<3;++Channel)
                {
                    const double LightRgb=Channel==0?1:Channel==1?double(.7f):double(.4f);
                    RgbTable[FacetIndex][Channel]=float(Lit?Facet.AlbedoRoughness[Channel]*LightRgb*Scale*
                        std::max(0.,TraceDot(UnitNormal,Light))/3.141592653589793:0);
                }
            }
            auto Rgb = [&RgbTable](uint32_t FacetIndex) { return RgbTable[FacetIndex]; };
            // 📝 Calibration uses only its separate pilot stream;held-out output is not available here.
            std::array<uint64_t,32> PilotCosts{};
            uint64_t PilotRotate=0;
            for (uint32_t Packet=0;Packet<4096;++Packet)
            {
                const uint32_t Seed=RgbQuartetHash((Packet+0x13579bdfu)^(Step*0x6c8e9cf5u));
                uint32_t SuffixMask=0,ConnectionMask=0;
                for (uint32_t Lane=0;Lane<4;++Lane)
                {
                    const bool Refresh=Reset || Lane/2==Step%2;
                    if (Refresh)
                    {
                        const uint32_t First=RgbQuartetHash(Seed^((3*Lane+1)*0x9e3779b9u))%9;
                        const uint32_t Second=RgbQuartetHash(Seed^((3*Lane+2)*0x9e3779b9u))%9;
                        const double FirstTarget=First<8?1./64+Geometry[First]:0;
                        const double SecondTarget=Second<8?1./64+Geometry[Second]:0;
                        const double Sum=FirstTarget+SecondTarget;
                        uint32_t Selected=UINT32_MAX;
                        if (Sum>0)
                        {
                            const double Uniform=double(RgbQuartetHash(Seed^((3*Lane+3)*0x9e3779b9u))>>8)/16777216;
                            Selected=Uniform<FirstTarget/Sum?First:Second;
                            if ((SuffixMask & (1u<<Selected))==0) ++PilotRotate;
                            SuffixMask |= 1u<<Selected;
                        }
                        PilotDescriptors[Packet][Lane]=Selected;
                    }
                    const uint32_t Selected=PilotDescriptors[Packet][Lane];
                    if (Selected<8 && Geometry[Selected]>0 &&
                        (RgbTable[Selected][0]>0 || RgbTable[Selected][1]>0 || RgbTable[Selected][2]>0))
                    {
                        if ((ConnectionMask & (1u<<Selected))==0) ++PilotRotate;
                        ConnectionMask |= 1u<<Selected;
                    }
                }
                SuffixMask=0; ConnectionMask=0;
                uint32_t Queries=0;
                for (uint32_t Draw=0;Draw<32;++Draw)
                {
                    const uint32_t Candidate=RgbQuartetHash(Seed^((3*(Draw/2)+Draw%2+1)*0x9e3779b9u))%9;
                    if (Candidate<8 && Geometry[Candidate]>0)
                    {
                        if ((SuffixMask & (1u<<Candidate))==0) ++Queries;
                        SuffixMask |= 1u<<Candidate;
                        if (RgbTable[Candidate][0]>0 || RgbTable[Candidate][1]>0 || RgbTable[Candidate][2]>0)
                        {
                            if ((ConnectionMask & (1u<<Candidate))==0) ++Queries;
                            ConnectionMask |= 1u<<Candidate;
                        }
                    }
                    PilotCosts[Draw]+=Queries;
                }
            }
            const bool Feasible=PilotRotate>=PilotCosts.front() && PilotRotate<=PilotCosts.back();
            uint32_t Lower=1,Upper=1;
            double Probability=0;
            if (PilotRotate>PilotCosts.back()) Lower=Upper=32;
            else if (PilotRotate>PilotCosts.front())
            {
                Upper=2;
                while (Upper<32 && PilotCosts[Upper-1]<PilotRotate) ++Upper;
                Lower=Upper-1;
                Probability=double(PilotRotate-PilotCosts[Lower-1])/
                    double(PilotCosts[Upper-1]-PilotCosts[Lower-1]);
            }
            std::printf("QUERY_CALIBRATION_ALLOCATION fixture=%u step=%u lower=%u upper=%u probability=%.17g "
                        "pilot_rotate=%llu pilot_lower=%llu pilot_upper=%llu feasible=%u\n",Fixture,Step,Lower,Upper,
                        Probability,(unsigned long long)PilotRotate,(unsigned long long)PilotCosts[Lower-1],
                        (unsigned long long)PilotCosts[Upper-1],unsigned(Feasible));
            auto ProposalCount = [=](uint32_t Packet)
            {
                const uint32_t Coin=RgbQuartetHash((Packet+0xdecafbadu)^(Step*0x85ebca6bu)^(Fixture*0xc2b2ae35u));
                return double(Coin)/4294967296.0<Probability?Upper:Lower;
            };
            for (uint32_t Pixel=0;Pixel<Count;++Pixel)
            {
                const uint32_t Seed=RgbQuartetHash((Pixel%Samples+0x90abcdefu)^(Step*0x6c8e9cf5u));
                const std::array<uint32_t,4> First{Seed,Pixel/Samples,Step,Pixel/Samples==2?ProposalCount(Pixel%Samples):8};
                const std::array<uint32_t,2> Second{Epoch,uint32_t(Step==0)};
                std::memcpy(Scene.Receivers[Pixel].PositionHit.data(),First.data(),16);
                std::memcpy(Scene.Receivers[Pixel].NormalRoughness.data(),Second.data(),8);
                Scene.Receivers[Pixel].NormalRoughness[2]=Scale;
                Scene.Receivers[Pixel].ViewMetallic={Receiver[0],Receiver[1],Receiver[2],0};
            }
            Compute.ExecuteDiffuse(Scene,2,17,true,false,Step==0);
            const auto* Storage=static_cast<const std::array<float,4>*>(Compute.Buffers[6].Address);
            double MaximumEstimate=0,MaximumWeight=0;
            std::array<uint64_t,4> DrawTotals{},KeptTotals{},EvaluationTotals{},SuffixTotals{},ConnectionTotals{};
            for (uint32_t Pixel=0;Pixel<Count;++Pixel)
            {
                const uint32_t Method=Pixel/Samples;
                const uint32_t Seed=RgbQuartetHash((Pixel%Samples+0x90abcdefu)^(Step*0x6c8e9cf5u));
                std::array<double,3> Estimate{};
                uint32_t Draws=0,Kept=0,Evaluations=0,SuffixQueries=0,ConnectionQueries=0;
                uint32_t SuffixMask=0,ConnectionMask=0;
                for (uint32_t Lane=0;Lane<4;++Lane)
                {
                    const bool Refresh=Reset || Method==1 || Lane/2==Step%2;
                    const bool Generate=Method<2 && Refresh;
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
                            Estimate[Channel]+=Expected.Outgoing[Channel]*Weights[Pixel][Lane]*Geometry[Expected.FacetIndex]*double(Visibility[Expected.FacetIndex])/4;
                        }
                        ++Evaluations;
                        const uint32_t FacetIndex=Expected.FacetIndex;
                        if (Refresh)
                        {
                            if (!Elide || (SuffixMask & (1u<<FacetIndex))==0) ++SuffixQueries;
                            SuffixMask |= 1u<<FacetIndex;
                        }
                        if (Elide && Geometry[FacetIndex]>0 &&
                            std::any_of(Expected.Outgoing.begin(),Expected.Outgoing.end(),[](float Radiance)
                            {
                                return Radiance>0;
                            }))
                        {
                            if ((ConnectionMask & (1u<<FacetIndex))==0) ++ConnectionQueries;
                            ConnectionMask |= 1u<<FacetIndex;
                        }
                    }
                    Previous[Pixel][Lane]=Actual;
                }
                if (Method>=2)
                {
                    Draws=Method==2?ProposalCount(Pixel%Samples):8;
                    for (uint32_t Draw=0;Draw<Draws;++Draw)
                    {
                        const uint32_t Candidate=RgbQuartetHash(Seed^((3*(Draw/2)+Draw%2+1)*0x9e3779b9u))%9;
                        if (Candidate>=8) continue;
                        ++Evaluations;
                        const auto Outgoing=Rgb(Candidate);
                        if (Geometry[Candidate]>0)
                        {
                            if ((SuffixMask & (1u<<Candidate))==0) ++SuffixQueries;
                            SuffixMask |= 1u<<Candidate;
                            if (Outgoing[0]>0 || Outgoing[1]>0 || Outgoing[2]>0)
                            {
                                if ((ConnectionMask & (1u<<Candidate))==0) ++ConnectionQueries;
                                ConnectionMask |= 1u<<Candidate;
                            }
                        }
                        for (uint32_t Channel=0;Channel<3;++Channel)
                            Estimate[Channel]+=Outgoing[Channel]*Geometry[Candidate]*double(Visibility[Candidate]);
                    }
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
                if (!Elide)
                {
                    if (Method>=2) SuffixQueries=Evaluations;
                    ConnectionQueries=Evaluations;
                }
                const auto Query=Storage[10*Count+Pixel];
                Require(Query[0]==float(SuffixQueries) && Query[1]==float(Method>=2?ConnectionQueries:0) &&
                        Query[2]==0 && Query[3]==float(Method<2?ConnectionQueries:0),"Traced quartet query counts differ");
                SuffixTotals[Method]+=SuffixQueries; ConnectionTotals[Method]+=ConnectionQueries;
                DrawTotals[Method]+=Draws; KeptTotals[Method]+=Kept; EvaluationTotals[Method]+=Evaluations;
            }
            std::printf("%s_CASE fixture=%u step=%u reset=%u estimate_error=%.12g weight_error=%.12g\n",
                        Label,Fixture,Step,unsigned(Reset),MaximumEstimate,MaximumWeight);
            for (uint32_t Method=0;Method<4;++Method)
                std::printf("%s_WORK fixture=%u step=%u method=%u draws=%llu kept=%llu evaluations=%llu suffix=%llu connection=%llu\n",
                            Label,Fixture,Step,Method,(unsigned long long)DrawTotals[Method],(unsigned long long)KeptTotals[Method],
                            (unsigned long long)EvaluationTotals[Method],(unsigned long long)SuffixTotals[Method],
                            (unsigned long long)ConnectionTotals[Method]);
            std::printf("%s_EXACT fixture=%u step=%u",Label,Fixture,Step);
            for (uint32_t Channel=0;Channel<3;++Channel)
            {
                double Exact=0;
                for (uint32_t FacetIndex=0;FacetIndex<8;++FacetIndex) Exact+=Rgb(FacetIndex)[Channel]*Geometry[FacetIndex]*double(Visibility[FacetIndex]);
                std::printf(" channel%u=%.17g",Channel,Exact);
            }
            std::printf("\n");
            std::ofstream Capture(Directory+"/QueryCalibration"+std::to_string(Fixture)+"-"+std::to_string(Step)+".f32",std::ios::binary);
            Capture.exceptions(std::ios::failbit|std::ios::badbit);
            Capture.write(reinterpret_cast<const char*>(Storage),19*Count*16);
            std::fflush(stdout);
        }
    }
    Require(Ambiguous<=16 && RetainedChecks==178176,"Persistent ambiguity or retention count differs");
    std::printf("%s_DECISION correctness=1 retained_checks=%llu ambiguous=%u production_promote=0\n",
                Label,(unsigned long long)RetainedChecks,Ambiguous);
}
}
