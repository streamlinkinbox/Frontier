/*============================================================================================================================================
                                                     AREARECONNECTIONVALIDATION.H
============================================================================================================================================*/
// 🧩 Independently integrate continuous facet area and replay GPU PDFs,visibility and per-draw transport.
#pragma once

namespace
{
struct AreaTransport
{
    std::array<double,3> Integrand{};
    std::array<double,3> Incident{};
    double              Squared = 0;
    double              Cosine  = 0;
    bool                SuffixVisible = false;
    bool                ConnectionVisible = false;
};

AreaTransport EvaluateAreaTransport(
    const LightComputeArchive& Scene,
    uint32_t                   Index,
    double                     U,
    double                     V,
    const TraceVector&         Receiver)
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
    const std::array<double,3> LightRgb{1,double(.7f),double(.4f)},ReceiverRgb{double(.6f),double(.5f),double(.4f)};
    for (uint32_t Channel=0;Channel<3;++Channel)
    {
        const double Outgoing=Transport.SuffixVisible?Facet.AlbedoRoughness[Channel]*LightRgb[Channel]*
            std::max(0.,TraceDot(Normal,Light))/3.141592653589793:0;
        Transport.Incident[Channel]=Outgoing*ReceiverRgb[Channel]/3.141592653589793*
            std::max(0.,Direction[2])*double(Transport.ConnectionVisible);
        Transport.Integrand[Channel]=Transport.Incident[Channel]*std::max(0.,Transport.Cosine)/Transport.Squared;
    }
    return Transport;
}

double ContinuousFacetArea(const LightComputeFacet& Facet)
{
    TraceVector First{},Second{};
    for (uint32_t Axis=0;Axis<3;++Axis)
    {
        First[Axis]=double(Facet.B[Axis])-Facet.A[Axis];
        Second[Axis]=double(Facet.C[Axis])-Facet.A[Axis];
    }
    const auto Cross=TraceCross(First,Second);
    return .5*std::sqrt(TraceDot(Cross,Cross));
}

void ValidateAreaReconnection(const char* Driver,const std::string& Shaders,const std::string& Directory)
{
    constexpr uint32_t Samples=2048,Count=Samples*2;
    auto Scene=RgbTraceScene(0);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene,Shaders,false,false,true);
    std::filesystem::create_directories(Directory);
    const std::array<TraceVector,3> Receivers{{{0,0,4},{4,-2,4},{double(.8f),-2,4}}};
    uint32_t Ambiguous=0;
    std::printf("AREA_RECONNECTION_DEVICE name=%s type=%u samples=2048 draws=8 methods=2 cases=9\n",
                Compute.Properties.deviceName,unsigned(Compute.Properties.deviceType));
    for (uint32_t Case=0;Case<9;++Case)
    {
        Scene=RgbTraceScene(Case/3);
        const auto Receiver=Receivers[Case%3];
        std::vector<double> Areas;
        double TotalArea=0;
        for (const auto& Facet:Scene.Surfaces)
        {
            Areas.push_back(ContinuousFacetArea(Facet));
            TotalArea+=Areas.back();
        }
        const uint32_t FacetCount=uint32_t(Areas.size());
        for (uint32_t Pixel=0;Pixel<Count;++Pixel)
        {
            const uint32_t Seed=RgbQuartetHash((Pixel%Samples+0x739ab531u)^(Case*0x6c8e9cf5u));
            const std::array<uint32_t,2> Identity{Seed,Pixel/Samples};
            std::memcpy(Scene.Receivers[Pixel].PositionHit.data(),Identity.data(),8);
            Scene.Receivers[Pixel].ViewMetallic={float(Receiver[0]),float(Receiver[1]),float(Receiver[2]),0};
        }
        Compute.ExecuteDiffuse(Scene,2,17,true,false,true);
        const auto* Storage=static_cast<const std::array<float,4>*>(Compute.Buffers[6].Address);
        double Maximum=0,MaximumDensity=0;
        uint32_t Grazing=0,SuffixBlocked=0,ConnectionBlocked=0;
        for (uint32_t Pixel=0;Pixel<Count;++Pixel)
        {
            const uint32_t Method=Pixel/Samples;
            const uint32_t Seed=RgbQuartetHash((Pixel%Samples+0x739ab531u)^(Case*0x6c8e9cf5u));
            std::array<double,3> Estimate{},DirectionEstimate{};
            double WrongArea=0,WrongJacobian=0;
            for (uint32_t Draw=0;Draw<8;++Draw)
            {
                const auto First=Storage[(2+2*Draw)*Count+Pixel];
                const auto Second=Storage[(3+2*Draw)*Count+Pixel];
                uint32_t Packed=0;
                std::memcpy(&Packed,&First[0],4);
                const uint32_t Index=Packed&0xffffu;
                Require(Index<FacetCount,"Continuous proposal index outside surface support");
                const double Choice=double(RgbQuartetHash(Seed^((3*Draw+1)*0x9e3779b9u))>>8)/16777216;
                const double Uniform=double(RgbQuartetHash(Seed^((3*Draw+2)*0x9e3779b9u))>>8)/16777216;
                const double Other=double(RgbQuartetHash(Seed^((3*Draw+3)*0x9e3779b9u))>>8)/16777216;
                const double U=1-std::sqrt(Uniform),V=std::sqrt(Uniform)*Other;
                Require(std::abs(First[1]-U)<2e-6 && std::abs(First[2]-V)<2e-6 &&
                        First[1]>=0 && First[2]>=0 && First[1]+First[2]<=1.000001f,"Nonuniform barycentric map");
                uint32_t Expected=std::min(uint32_t(Choice*FacetCount),FacetCount-1);
                double Lower=0,Upper=0;
                if (Method==1)
                {
                    for (uint32_t Candidate=0;Candidate<FacetCount;++Candidate)
                    {
                        Upper=Lower+Areas[Candidate]/TotalArea;
                        if (Choice<Upper || Candidate==FacetCount-1) { Expected=Candidate; break; }
                        Lower=Upper;
                    }
                }
                if (Index!=Expected)
                {
                    Require(Method==1 && std::min(std::abs(Choice-Lower),std::abs(Choice-Upper))<2e-6,
                            "Area-proportional selector differs");
                    ++Ambiguous;
                }
                const double Probability=Method==0?1./FacetCount:Areas[Index]/TotalArea;
                const double Density=Probability/Areas[Index];
                // 📝 Reconstruct rays from stored float barycentrics;their RNG mapping was checked independently above.
                const auto Transport=EvaluateAreaTransport(Scene,Index,First[1],First[2],Receiver);
                const double DirectionDensity=std::abs(Transport.Cosine)>0?
                    Density*Transport.Squared/std::abs(Transport.Cosine):0;
                Require(((Packed>>16)&1u)==uint32_t(Transport.SuffixVisible) &&
                        ((Packed>>17)&1u)==uint32_t(Transport.ConnectionVisible),"Continuous double-ray visibility differs");
                if (!Transport.SuffixVisible) ++SuffixBlocked;
                if (!Transport.ConnectionVisible) ++ConnectionBlocked;
                if (Transport.Cosine==0) ++Grazing;
                for (const auto& Pair:std::array<std::array<double,2>,2>{{{First[3],Density},{Second[3],DirectionDensity}}})
                {
                    const double Error=std::abs(Pair[0]-Pair[1])/std::max(1.,std::abs(Pair[1]));
                    Require(std::isfinite(Pair[0]) && Pair[0]>=0 && Error<2e-5,"Area/direction density differs");
                    MaximumDensity=std::max(MaximumDensity,Error);
                }
                for (uint32_t Channel=0;Channel<3;++Channel)
                {
                    const double Contribution=Transport.Integrand[Channel]/Density;
                    const double Error=std::abs(Second[Channel]-Contribution)/std::max(1.,std::abs(Contribution));
                    Require(std::isfinite(Second[Channel]) && Error<2e-5,"Area contribution differs from double reference");
                    Maximum=std::max(Maximum,Error);
                    Estimate[Channel]+=Contribution/8;
                    if (Transport.Cosine>0) DirectionEstimate[Channel]+=Transport.Incident[Channel]/DirectionDensity/8;
                    WrongArea+=Transport.Integrand[Channel]/Probability/24;
                    if (Transport.Cosine>0) WrongJacobian+=Transport.Incident[Channel]/Density/24;
                }
            }
            for (uint32_t Channel=0;Channel<3;++Channel)
            {
                for (const auto& Pair:std::array<std::array<double,2>,2>{{{Storage[Pixel][Channel],Estimate[Channel]},
                    {Storage[Count+Pixel][Channel],DirectionEstimate[Channel]}}})
                {
                    const double Error=std::abs(Pair[0]-Pair[1])/std::max(1.,std::abs(Pair[1]));
                    Require(std::isfinite(Pair[0]) && Error<2e-5,"Continuous estimate differs from double reference");
                    Maximum=std::max(Maximum,Error);
                }
                Require(std::abs(Storage[Pixel][Channel]-Storage[Count+Pixel][Channel])<2e-5,
                        "Area/direction estimator disagreement");
            }
            const auto Work=Storage[18*Count+Pixel];
            Require(Work[0]==8 && Work[1]==8,"Fresh continuous ray counts differ");
            Require(std::abs(Work[2]-WrongArea)<2e-5 && std::abs(Work[3]-WrongJacobian)<2e-5,
                    "Deliberate PDF omission differs from CPU");
        }
        std::printf("AREA_RECONNECTION_CASE case=%u fixture=%u receiver=%u paths=32768 suffix=32768 connection=32768 "
                    "grazing=%u suffix_blocked=%u connection_blocked=%u error=%.12g density_error=%.12g\n",
                    Case,Case/3,Case%3,Grazing,SuffixBlocked,ConnectionBlocked,Maximum,MaximumDensity);
        for (uint32_t Resolution:{128u,256u})
        {
            std::array<double,3> Integral{};
            for (uint32_t Index=0;Index<FacetCount;++Index)
                for (uint32_t Row=0;Row<Resolution;++Row)
                    for (uint32_t Column=0;Column<Resolution;++Column)
                    {
                        const double Root=std::sqrt((Row+.5)/Resolution);
                        const auto Transport=EvaluateAreaTransport(Scene,Index,1-Root,Root*(Column+.5)/Resolution,Receiver);
                        for (uint32_t Channel=0;Channel<3;++Channel)
                            Integral[Channel]+=Areas[Index]*Transport.Integrand[Channel]/(Resolution*Resolution);
                    }
            std::printf("AREA_RECONNECTION_REFERENCE case=%u resolution=%u r=%.17g g=%.17g b=%.17g\n",
                        Case,Resolution,Integral[0],Integral[1],Integral[2]);
        }
        std::ofstream Capture(Directory+"/AreaReconnection"+std::to_string(Case)+".f32",std::ios::binary);
        Capture.exceptions(std::ios::failbit|std::ios::badbit);
        Capture.write(reinterpret_cast<const char*>(Storage),19*Count*16);
        std::fflush(stdout);
    }
    Require(Ambiguous<=16,"Too many continuous selector ambiguities");
    std::printf("AREA_RECONNECTION_DECISION correctness=1 ambiguous=%u production_promote=0\n",Ambiguous);
}
}
