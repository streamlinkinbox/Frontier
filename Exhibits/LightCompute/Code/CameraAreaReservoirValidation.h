/*============================================================================================================================================
                                                      CAMERAAREARESERVOIRVALIDATION.H
============================================================================================================================================*/
// 🧩 Replay continuous-area RIS selection,normalizers and selected rays independently against raw controls.
#pragma once

namespace
{
LightComputeReceiver CameraAreaReceiver(uint32_t Pixel)
{
    LightComputeReceiver Surface{};
    const float X=-2+4*(float(Pixel%32)+.5f)/32;
    const float Y=-2+4*(float((Pixel/32)%32)+.5f)/32;
    // 📝 Orthographic primary ray from (X,Y,2) along +z hits the shading plane at z4.
    Surface.ViewMetallic={X,Y,4,0};
    const auto Normal=TraceNormalize(TraceVector{.35*std::sin(2*X),.35*std::cos(2*Y),-1});
    Surface.NormalRoughness={float(Normal[0]),float(Normal[1]),float(Normal[2]),.7f};
    Surface.AlbedoIor=X<-.65f?std::array<float,4>{.8f,.2f,.15f,1}:
        X<.65f?std::array<float,4>{.2f,.7f,.3f,1}:std::array<float,4>{.2f,.3f,.8f,1};
    return Surface;
}

AreaTransport EvaluateCameraAreaTransport(
    const LightComputeArchive& Scene,
    uint32_t                   Index,
    double                     U,
    double                     V,
    const TraceVector&         Receiver,
    const LightComputeReceiver& ReceiverSurface)
{
    const auto& Facet=Scene.Surfaces[Index];
    TraceVector Point{},Normal{},Origin{},Delta{};
    for (uint32_t Axis=0;Axis<3;++Axis)
    {
        Point[Axis]=Facet.A[Axis]+(double(Facet.B[Axis])-Facet.A[Axis])*U+
            (double(Facet.C[Axis])-Facet.A[Axis])*V;
        Normal[Axis]=Facet.NormalA[Axis]*(1-U-V)+Facet.NormalB[Axis]*U+Facet.NormalC[Axis]*V;
        Delta[Axis]=Receiver[Axis]-Point[Axis];
    }
    Normal=TraceNormalize(Normal);
    for (uint32_t Axis=0;Axis<3;++Axis) Origin[Axis]=Point[Axis]+Normal[Axis]*.002f;
    AreaTransport Transport;
    Transport.Squared=TraceDot(Delta,Delta);
    const auto Direction=TraceNormalize(Delta);
    Transport.Cosine=TraceDot(Normal,Direction);
    const TraceVector Light{double(.8f),0,double(.6f)};
    Transport.SuffixVisible=!TraceBlocked(Scene,Origin,Light,100);
    Transport.ConnectionVisible=!TraceBlocked(Scene,Origin,Direction,std::sqrt(Transport.Squared)-.004f);
    const std::array<double,3> LightRgb{1,double(.7f),double(.4f)},ReceiverRgb{ReceiverSurface.AlbedoIor[0],ReceiverSurface.AlbedoIor[1],ReceiverSurface.AlbedoIor[2]};
    for (uint32_t Channel=0;Channel<3;++Channel)
    {
        const double Outgoing=Transport.SuffixVisible?Facet.AlbedoRoughness[Channel]*LightRgb[Channel]*
            std::max(0.,TraceDot(Normal,Light))/3.141592653589793:0;
        Transport.Incident[Channel]=Outgoing*ReceiverRgb[Channel]/3.141592653589793*
            std::max(0.,-(ReceiverSurface.NormalRoughness[0]*Direction[0]+
                ReceiverSurface.NormalRoughness[1]*Direction[1]+ReceiverSurface.NormalRoughness[2]*Direction[2]))*double(Transport.ConnectionVisible);
        Transport.Integrand[Channel]=Transport.Incident[Channel]*std::max(0.,Transport.Cosine)/Transport.Squared;
    }
    return Transport;
}

double EvaluateCameraAreaProxy(
    const LightComputeArchive& Scene,
    uint32_t                   Index,
    double                     U,
    double                     V,
    const TraceVector&         Receiver,
    const LightComputeReceiver& ReceiverSurface)
{
    const auto& Facet=Scene.Surfaces[Index];
    TraceVector Normal{},Delta{};
    for (uint32_t Axis=0;Axis<3;++Axis)
    {
        const double Point=Facet.A[Axis]+(double(Facet.B[Axis])-Facet.A[Axis])*U+
            (double(Facet.C[Axis])-Facet.A[Axis])*V;
        Delta[Axis]=Receiver[Axis]-Point;
        Normal[Axis]=Facet.NormalA[Axis]*(1-U-V)+Facet.NormalB[Axis]*U+Facet.NormalC[Axis]*V;
    }
    Normal=TraceNormalize(Normal);
    const double Squared=TraceDot(Delta,Delta);
    const auto Direction=TraceNormalize(Delta);
    const double Geometry=std::max(0.,TraceDot(Normal,Direction))*std::max(0.,-(ReceiverSurface.NormalRoughness[0]*Direction[0]+
        ReceiverSurface.NormalRoughness[1]*Direction[1]+ReceiverSurface.NormalRoughness[2]*Direction[2]))/Squared;
    const TraceVector Light{double(.8f),0,double(.6f)};
    const std::array<double,3> LightRgb{1,double(.7f),double(.4f)},ReceiverRgb{ReceiverSurface.AlbedoIor[0],ReceiverSurface.AlbedoIor[1],ReceiverSurface.AlbedoIor[2]};
    double Score=0;
    for (uint32_t Channel=0;Channel<3;++Channel)
        Score+=Facet.AlbedoRoughness[Channel]*LightRgb[Channel]*ReceiverRgb[Channel]*
            std::max(0.,TraceDot(Normal,Light))*Geometry/(3*3.141592653589793*3.141592653589793);
    return 1e-6+Score;
}

void ValidateCameraAreaReservoir(const char* Driver,const std::string& Shaders,const std::string& Directory)
{
    constexpr uint32_t Samples=4096,Count=Samples*6;
    auto Scene=RgbTraceScene(0);
    Scene.Width=192;Scene.Height=128;Scene.Receivers.assign(Count,Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene,Shaders,false,false,true);
    std::filesystem::create_directories(Directory);
    uint32_t Ambiguous=0;
    uint64_t SelectedChecks=0,CandidateChecks=0;
    std::printf("CAMERA_AREA_RESERVOIR_DEVICE name=%s type=%u pixels=1024 packets=4 methods=6 cases=3\n",
                Compute.Properties.deviceName,unsigned(Compute.Properties.deviceType));
    for (uint32_t Case=0;Case<3;++Case)
    {
        Scene=RgbTraceScene(Case);
        Scene.Width=192;Scene.Height=128;Scene.Receivers.resize(Count,Scene.Receivers.front());
        std::vector<double> Areas;
        double TotalArea=0;
        for (const auto& Facet:Scene.Surfaces)
        {
            Areas.push_back(ContinuousFacetArea(Facet));TotalArea+=Areas.back();
        }
        const uint32_t FacetCount=uint32_t(Areas.size());
        for (uint32_t Pixel=0;Pixel<Count;++Pixel)
        {
            const auto ReceiverSurface=CameraAreaReceiver(Pixel%1024);
            const TraceVector Receiver{ReceiverSurface.ViewMetallic[0],ReceiverSurface.ViewMetallic[1],4};
            const uint32_t Seed=RgbQuartetHash((Pixel%Samples+0x739ab531u)^(Case*0x6c8e9cf5u));
            Scene.Receivers[Pixel]=ReceiverSurface;
            const std::array<uint32_t,2> Identity{Seed,Pixel/Samples};
            std::memcpy(Scene.Receivers[Pixel].PositionHit.data(),Identity.data(),8);
            Scene.Receivers[Pixel].ViewMetallic={float(Receiver[0]),float(Receiver[1]),float(Receiver[2]),0};
        }
        Compute.ExecuteDiffuse(Scene,2,17,true,false,true);
        const auto* Storage=static_cast<const std::array<float,4>*>(Compute.Buffers[6].Address);
        double Maximum=0,MaximumWeight=0;
        for (uint32_t Pixel=0;Pixel<Count;++Pixel)
        {
            const uint32_t Method=Pixel/Samples;
            const uint32_t ProposalMeasure=Method%2;
            const auto ReceiverSurface=CameraAreaReceiver(Pixel%1024);
            const TraceVector Receiver{ReceiverSurface.ViewMetallic[0],ReceiverSurface.ViewMetallic[1],4};
            const uint32_t Seed=RgbQuartetHash((Pixel%Samples+0x739ab531u)^(Case*0x6c8e9cf5u));
            std::array<double,3> Estimate{},Negative{};
            for (uint32_t Lane=0;Lane<4;++Lane)
            {
                std::array<uint32_t,2> Packed{},Indices{};
                std::array<double,2> Density{},Score{};
                std::array<AreaTransport,2> Transport{};
                const auto Contribution=Storage[(4+4*Lane)*Count+Pixel];
                const auto Telemetry=Storage[(5+4*Lane)*Count+Pixel];
                const uint32_t Proposals=Method<2 || Method>=4?2:1;
                for (uint32_t Proposal=0;Proposal<2;++Proposal)
                {
                    const auto Wire=Storage[(2+4*Lane+Proposal)*Count+Pixel];
                    std::memcpy(&Packed[Proposal],&Wire[0],4);
                    if (Proposal>=Proposals)
                    {
                        Require(Packed[Proposal]==UINT32_MAX && Wire[1]==0 && Wire[2]==0 && Wire[3]==0,
                                "Noncanonical unused raw proposal");
                        continue;
                    }
                    ++CandidateChecks;
                    const uint32_t Draw=2*Lane+Proposal;
                    const double Choice=double(RgbQuartetHash(Seed^((3*Draw+1)*0x9e3779b9u))>>8)/16777216;
                    const double First=double(RgbQuartetHash(Seed^((3*Draw+2)*0x9e3779b9u))>>8)/16777216;
                    const double Second=double(RgbQuartetHash(Seed^((3*Draw+3)*0x9e3779b9u))>>8)/16777216;
                    Require(std::abs(Wire[1]-(1-std::sqrt(First)))<2e-6 &&
                            std::abs(Wire[2]-std::sqrt(First)*Second)<2e-6,"Area reservoir barycentric draw differs");
                    uint32_t Expected=std::min(uint32_t(Choice*FacetCount),FacetCount-1);
                    double Lower=0,Upper=0;
                    if (ProposalMeasure==1)
                    {
                        for (uint32_t Index=0;Index<FacetCount;++Index)
                        {
                            Upper=Lower+Areas[Index]/TotalArea;
                            if (Choice<Upper || Index==FacetCount-1) { Expected=Index;break; }
                            Lower=Upper;
                        }
                    }
                    Indices[Proposal]=Packed[Proposal]&0xffffu;
                    Require(Indices[Proposal]<FacetCount,"Foreign continuous RIS proposal");
                    if (Indices[Proposal]!=Expected)
                    {
                        Require(ProposalMeasure==1 && std::min(std::abs(Choice-Lower),std::abs(Choice-Upper))<2e-6,
                                "Continuous RIS facet selector differs");
                        ++Ambiguous;
                    }
                    const uint32_t Index=Indices[Proposal];
                    Density[Proposal]=ProposalMeasure==0?1./FacetCount/Areas[Index]:1./TotalArea;
                    Score[Proposal]=EvaluateCameraAreaProxy(Scene,Index,Wire[1],Wire[2],Receiver,ReceiverSurface);
                    Transport[Proposal]=EvaluateCameraAreaTransport(Scene,Index,Wire[1],Wire[2],Receiver,ReceiverSurface);
                    Require(std::isfinite(Wire[3]) && Wire[3]>0 &&
                            std::abs(Wire[3]-Density[Proposal])/std::max(1.,Density[Proposal])<2e-5,
                            "Continuous RIS proposal density differs");
                    Require(std::isfinite(Telemetry[Proposal]) && Telemetry[Proposal]>0 &&
                            std::abs(Telemetry[Proposal]-Score[Proposal])<2e-5,"Unoccluded RIS score differs");
                }
                uint32_t Selected=0;
                double Normalizer=1/Density[0],WrongNormalizer=1;
                if (Method<2)
                {
                    const double FirstWeight=Score[0]/Density[0],SecondWeight=Score[1]/Density[1];
                    const double Threshold=FirstWeight/(FirstWeight+SecondWeight);
                    const double Uniform=double(RgbQuartetHash(Seed^(0x7f4a7c15u+Lane*0x9e3779b9u))>>8)/16777216;
                    Selected=Uniform<Threshold?0:1;
                    Require(Telemetry[3]==float(Uniform),"RIS selector random domain differs");
                    if (uint32_t(Telemetry[2])!=Selected)
                    {
                        Require(std::abs(Uniform-Threshold)<2e-6,"RIS weight selector differs");
                        ++Ambiguous;Selected=uint32_t(Telemetry[2]);
                    }
                    Require(Selected<2,"Foreign RIS selected ordinal");
                    Normalizer=(FirstWeight+SecondWeight)/(2*Score[Selected]);
                    WrongNormalizer=(Score[0]+Score[1])/(2*Score[Selected]);
                }
                else Require(Telemetry[2]==0 && Telemetry[3]==0,"Raw path performed RIS selection");
                for (uint32_t Proposal=0;Proposal<Proposals;++Proposal)
                {
                    const bool Traced=Method>=2 || Proposal==Selected;
                    Require(((Packed[Proposal]>>18)&1u)==unsigned(Traced),"Unselected proposal traced");
                    if (!Traced)
                    {
                        Require(Packed[Proposal]==Indices[Proposal],"Untraced proposal contains visibility bits");
                        continue;
                    }
                    ++SelectedChecks;
                    Require(((Packed[Proposal]>>16)&1u)==unsigned(Transport[Proposal].SuffixVisible) &&
                            ((Packed[Proposal]>>17)&1u)==unsigned(Transport[Proposal].ConnectionVisible),
                            "RIS selected visibility differs from double rays");
                }
                const double WeightReference=Method>=4?0:Normalizer;
                const double WeightError=std::abs(Contribution[3]-WeightReference)/std::max(1.,WeightReference);
                if (!(std::isfinite(Contribution[3]) && WeightError<2e-5))
                    std::fprintf(stderr,"CAMERA_NUMERIC case=%u pixel=%u lane=%u selected=%u weight=%.12g cpu=%.12g error=%.12g scores=%.12g,%.12g cpu_scores=%.12g,%.12g\n",
                        Case,Pixel,Lane,Selected,double(Contribution[3]),WeightReference,WeightError,
                        double(Telemetry[0]),double(Telemetry[1]),Score[0],Score[1]);
                Require(std::isfinite(Contribution[3]) && WeightError<2e-5,"RIS normalizer differs");
                MaximumWeight=std::max(MaximumWeight,WeightError);
                for (uint32_t Channel=0;Channel<3;++Channel)
                {
                    double Expected=Transport[Selected].Integrand[Channel]*Normalizer;
                    double Wrong=Transport[Selected].Integrand[Channel]*WrongNormalizer;
                    if (Method>=4)
                    {
                        Expected=.5*(Transport[0].Integrand[Channel]/Density[0]+
                                     Transport[1].Integrand[Channel]/Density[1]);
                        Wrong=.5*(Transport[0].Integrand[Channel]+Transport[1].Integrand[Channel]);
                    }
                    const double Error=std::abs(Contribution[Channel]-Expected)/std::max(1.,std::abs(Expected));
                    Require(std::isfinite(Contribution[Channel]) && Error<2e-5,"RIS selected contribution differs");
                    Maximum=std::max(Maximum,Error);
                    Estimate[Channel]+=Expected/4;Negative[Channel]+=Wrong/4;
                }
            }
            for (uint32_t Channel=0;Channel<3;++Channel)
            {
                for (const auto& Pair:std::array<std::array<double,2>,2>{{{Storage[Pixel][Channel],Estimate[Channel]},
                    {Storage[Count+Pixel][Channel],Negative[Channel]}}})
                {
                    const double Error=std::abs(Pair[0]-Pair[1])/std::max(1.,std::abs(Pair[1]));
                    Require(std::isfinite(Pair[0]) && Error<2e-5,"Area reservoir output differs from double reference");
                    Maximum=std::max(Maximum,Error);
                }
            }
            const auto Work=Storage[18*Count+Pixel];
            Require(Work[0]==float(Method>=4?8:4) && Work[1]==Work[0] &&
                    Work[2]==float(Method<2 || Method>=4?8:4) && Work[3]==0,"RIS query/proposal telemetry differs");
        }
        std::printf("CAMERA_AREA_RESERVOIR_CASE case=%u error=%.12g normalizer_error=%.12g\n",Case,Maximum,MaximumWeight);
        std::ofstream Capture(Directory+"/CameraAreaReservoir"+std::to_string(Case)+".f32",std::ios::binary);
        Capture.exceptions(std::ios::failbit|std::ios::badbit);
        Capture.write(reinterpret_cast<const char*>(Storage),19*Count*16);
        // 📝 Disjoint reference domains,uniform raw8 only;24 packets/pixel/dispatch,four dispatches/bank.
        std::array<std::array<std::array<float,4>,1024>,2> Reference{};
        for (uint32_t Bank=0;Bank<2;++Bank)
        {
            std::array<std::array<double,3>,1024> Sum{};
            for (uint32_t Batch=0;Batch<4;++Batch)
            {
                for (uint32_t Pixel=0;Pixel<Count;++Pixel)
                {
                    Scene.Receivers[Pixel]=CameraAreaReceiver(Pixel%1024);
                    const uint32_t Seed=RgbQuartetHash((Pixel+0x28f735b1u)^((Bank*4+Batch)*0x9e3779b9u)^
                        (Case*0x6c8e9cf5u));
                    const std::array<uint32_t,2> Identity{Seed,4};
                    std::memcpy(Scene.Receivers[Pixel].PositionHit.data(),Identity.data(),8);
                }
                Compute.ExecuteDiffuse(Scene,2,17,true,false,true);
                for (uint32_t Pixel=0;Pixel<Count;++Pixel)
                    for (uint32_t Channel=0;Channel<3;++Channel)
                    {
                        Require(std::isfinite(Storage[Pixel][Channel]),"Nonfinite camera reference");
                        Sum[Pixel%1024][Channel]+=Storage[Pixel][Channel]/96.;
                    }
            }
            for (uint32_t Pixel=0;Pixel<1024;++Pixel)
                for (uint32_t Channel=0;Channel<3;++Channel)
                    Reference[Bank][Pixel][Channel]=float(Sum[Pixel][Channel]);
        }
        std::ofstream ReferenceCapture(Directory+"/CameraAreaReference"+std::to_string(Case)+".f32",std::ios::binary);
        ReferenceCapture.exceptions(std::ios::failbit|std::ios::badbit);
        ReferenceCapture.write(reinterpret_cast<const char*>(Reference.data()),2*1024*16);
        std::printf("CAMERA_AREA_REFERENCE case=%u banks=2 packets_per_bank_pixel=96 samples_per_bank_pixel=768\n",Case);
        std::fflush(stdout);
    }
    Require(Ambiguous<=16 && CandidateChecks==491520 && SelectedChecks==393216,"RIS replay coverage differs");
    std::printf("CAMERA_AREA_RESERVOIR_DECISION correctness=1 candidates=%llu traced=%llu ambiguous=%u production_promote=0\n",
                (unsigned long long)CandidateChecks,(unsigned long long)SelectedChecks,Ambiguous);
}
}
