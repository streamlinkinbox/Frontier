/*============================================================================================================================================
                                                        RGBTRACEVALIDATION.H
============================================================================================================================================*/
// 🧩 Compare actual RGB shadow/reconnection rays with independent double triangle intersections.
#pragma once

namespace
{
using TraceVector = std::array<double,3>;
TraceVector TraceCross(TraceVector First, TraceVector Second)
{
    return {First[1]*Second[2]-First[2]*Second[1],First[2]*Second[0]-First[0]*Second[2],First[0]*Second[1]-First[1]*Second[0]};
}
double TraceDot(TraceVector First, TraceVector Second)
{
    return First[0]*Second[0]+First[1]*Second[1]+First[2]*Second[2];
}
TraceVector TraceNormalize(TraceVector Vector)
{
    const double Length=std::sqrt(TraceDot(Vector,Vector));
    for (auto& Component:Vector) Component/=Length;
    return Vector;
}
bool TraceBlocked(const LightComputeArchive& Scene,TraceVector Origin,TraceVector Direction,double Far)
{
    for (const auto& Facet:Scene.Surfaces)
    {
        TraceVector First{},Second{},Delta{};
        for (uint32_t Axis=0;Axis<3;++Axis)
        {
            First[Axis]=double(Facet.B[Axis])-Facet.A[Axis];
            Second[Axis]=double(Facet.C[Axis])-Facet.A[Axis];
            Delta[Axis]=Origin[Axis]-Facet.A[Axis];
        }
        const auto Cross=TraceCross(Direction,Second);
        const double Determinant=TraceDot(First,Cross);
        if (std::abs(Determinant)<1e-12) continue;
        const double U=TraceDot(Delta,Cross)/Determinant;
        const auto Other=TraceCross(Delta,First);
        const double V=TraceDot(Direction,Other)/Determinant;
        const double Distance=TraceDot(Second,Other)/Determinant;
        if (U>=0 && V>=0 && U+V<=1 && Distance>.00001 && Distance<Far) return true;
    }
    return false;
}

LightComputeArchive RgbTraceScene(uint32_t Phase)
{
    auto Scene=TriangleReconnectionScene(0);
    std::vector<std::array<SpatialVector,3>> Triangles;
    for (uint32_t Index=0;Index<8;++Index)
    {
        const double Angle=Index*.7;
        SpatialVector Corner{2*std::cos(Angle),2*std::sin(Angle),Index*.2};
        Triangles.push_back({Corner,{Corner.X+.7,Corner.Y+.13,Corner.Z+.2*std::cos(Index)},
                                   {Corner.X-.1,Corner.Y+.6,Corner.Z+.3*std::sin(Index)}});
    }
    const double X=Phase==0?20:Phase==1?2.7:.8;
    const double Y=Phase==0?20:0;
    Triangles.push_back({SpatialVector{X,Y-.6,-2},SpatialVector{X,Y+.6,-2},SpatialVector{X,Y+.6,5}});
    Triangles.push_back({SpatialVector{X,Y-.6,-2},SpatialVector{X,Y+.6,5},SpatialVector{X,Y-.6,5}});
    TriangleVisibilitySpace Space; Space.Build(Triangles);
    Scene.Visibility=Space.PackFloat32(); Scene.Surfaces.clear();
    for (const auto& Corners:Scene.Visibility.Corners)
    {
        LightComputeFacet Facet{};Facet.A=Corners.A;Facet.B=Corners.B;Facet.C=Corners.C;
        TraceVector First{},Second{};
        for (uint32_t Axis=0;Axis<3;++Axis)
        {
            First[Axis]=double(Facet.B[Axis])-Facet.A[Axis];Second[Axis]=double(Facet.C[Axis])-Facet.A[Axis];
        }
        const auto Normal=TraceNormalize(TraceCross(First,Second));
        for (uint32_t Axis=0;Axis<3;++Axis)
            Facet.NormalA[Axis]=Facet.NormalB[Axis]=Facet.NormalC[Axis]=float(Normal[Axis]);
        const uint32_t Index=uint32_t(Scene.Surfaces.size());
        Facet.AlbedoRoughness={float(2+Index%5)/8,float(1+Index%4)/6,float(1+Index%3)/5,.7f};
        if (Phase>=3) Facet.AlbedoRoughness[0]*=.5f;
        Scene.Surfaces.push_back(Facet);
    }
    Scene.Width=64;Scene.Height=64;Scene.Receivers.assign(4096,Scene.Receivers.front());
    return Scene;
}

void ValidateRgbTrace(const char* Driver,const std::string& Shaders,const std::string& Directory)
{
    constexpr uint32_t Count=4096;
    auto Scene=RgbTraceScene(0);
    LightComputeSubmission Compute(Driver);Compute.Prepare(Scene,Shaders,false,false,true);
    std::filesystem::create_directories(Directory);
    const std::array<TraceVector,4> Receivers{{{0,0,4},{0,0,-3},{4,0,4},{-3,3,2}}};
    uint32_t NegativeTotal=0;
    std::printf("RGB_TRACE_DEVICE name=%s type=%u\n",Compute.Properties.deviceName,unsigned(Compute.Properties.deviceType));
    for (uint32_t Phase=0;Phase<6;++Phase)
    {
        Scene=RgbTraceScene(Phase);
        const uint32_t Epoch=Phase>=3?10:7+Phase,Birth=Phase==4?9:Epoch;
        const float Scale=Phase>=3?2.f:1.f;
        for (uint32_t Pixel=0;Pixel<Count;++Pixel)
        {
            const uint32_t Index=Pixel%10;
            const float U=.15f+float((Pixel*17)%31)/100,V=.15f+float((Pixel*7)%23)/100;
            const std::array<uint32_t,2> Identity{Index,Birth};
            std::memcpy(Scene.Receivers[Pixel].PositionHit.data(),Identity.data(),8);
            Scene.Receivers[Pixel].PositionHit[2]=U;Scene.Receivers[Pixel].PositionHit[3]=V;
            std::memcpy(Scene.Receivers[Pixel].NormalRoughness.data(),&Epoch,4);
            Scene.Receivers[Pixel].NormalRoughness[1]=Scale;
            const auto Receiver=Receivers[(Pixel/10)%4];
            Scene.Receivers[Pixel].ViewMetallic={float(Receiver[0]),float(Receiver[1]),float(Receiver[2]),0};
        }
        Compute.ExecuteDiffuse(Scene,2,17,true,false,true);
        const auto* Storage=static_cast<const std::array<float,4>*>(Compute.Buffers[6].Address);
        double Maximum=0;uint32_t SuffixBlocked=0,ReceiverBlocked=0,Negative=0;
        for (uint32_t Pixel=0;Pixel<Count;++Pixel)
        {
            const bool Valid=Birth==Epoch;
            for (uint32_t Query=0;Query<4;++Query)
                Require(Storage[8*Count+Pixel][Query]==float(Valid),"RGB ray count differs");
            if (!Valid)
            {
                for (uint32_t Slot:{0u,1u,2u,6u,7u})
                    for (float Component:Storage[Slot*Count+Pixel]) Require(Component==0,"Stale RGB descriptor traced or contributed");
                continue;
            }
            const uint32_t Index=Pixel%10;
            const auto& Facet=Scene.Surfaces[Index];const auto& Input=Scene.Receivers[Pixel];
            const double U=Input.PositionHit[2],V=Input.PositionHit[3];
            TraceVector Point{},Normal{},Origin{},Delta{};
            for (uint32_t Axis=0;Axis<3;++Axis)
            {
                Point[Axis]=Facet.A[Axis]*(1-U-V)+Facet.B[Axis]*U+Facet.C[Axis]*V;
                Normal[Axis]=Facet.NormalA[Axis]*(1-U-V)+Facet.NormalB[Axis]*U+Facet.NormalC[Axis]*V;
            }
            Normal=TraceNormalize(Normal);
            for (uint32_t Axis=0;Axis<3;++Axis)
            {
                Origin[Axis]=Point[Axis]+Normal[Axis]*.002f;Delta[Axis]=Input.ViewMetallic[Axis]-Point[Axis];
            }
            const TraceVector Light{double(.8f),0,double(.6f)};
            const bool SourceVisible=!TraceBlocked(Scene,Origin,Light,100);
            const double Distance=std::sqrt(TraceDot(Delta,Delta));
            const auto Direction=TraceNormalize(Delta);
            const bool Visible=!TraceBlocked(Scene,Origin,Direction,Distance-.004f);
            const double Geometry=std::max(0.,TraceDot(Normal,Direction))/TraceDot(Delta,Delta);
            Require(Storage[Pixel][3]==float(Visible) && Storage[Count+Pixel][3]==float(Visible),"Reconnection ray disagrees with CPU");
            Require(Storage[6*Count+Pixel][3]==float(SourceVisible) && Storage[7*Count+Pixel][3]==float(SourceVisible),"Suffix shadow ray disagrees with CPU");
            SuffixBlocked+=!SourceVisible;ReceiverBlocked+=!Visible;
            for (uint32_t Channel=0;Channel<3;++Channel)
            {
                const double LightRgb=Channel==0?1:Channel==1?double(.7f):double(.4f);
                const double Rgb=SourceVisible?Facet.AlbedoRoughness[Channel]*LightRgb*Scale*std::max(0.,TraceDot(Normal,Light))/3.141592653589793:0;
                const double Expected=Rgb*Geometry*double(Visible);
                for (uint32_t Slot:{0u,1u})
                {
                    const double Error=std::abs(Storage[Slot*Count+Pixel][Channel]-Expected)/std::max(1.,Expected);
                    Require(std::isfinite(Storage[Slot*Count+Pixel][Channel]) && Error<2e-5,"Traced RGB estimate differs from CPU");
                    Maximum=std::max(Maximum,Error);
                }
                Require(std::abs(Storage[6*Count+Pixel][Channel]-Rgb)<2e-5,"Cached traced suffix differs from CPU");
                Require(std::memcmp(Storage[Pixel].data(),Storage[Count+Pixel].data(),16)==0,"Cached and inline RGB disagree");
            }
            if (!Visible && SourceVisible && Geometry>0 && Storage[2*Count+Pixel][0]>1e-8) ++Negative;
        }
        NegativeTotal+=Negative;
        std::printf("RGB_TRACE_CASE phase=%u valid=%u suffix_blocked=%u receiver_blocked=%u omitted_visibility_failures=%u error=%.12g\n",
                    Phase,Phase==4?0:Count,SuffixBlocked,ReceiverBlocked,Negative,Maximum);
        std::ofstream Capture(Directory+"/RgbTrace"+std::to_string(Phase)+".f32",std::ios::binary);
        Capture.exceptions(std::ios::failbit|std::ios::badbit);
        Capture.write(reinterpret_cast<const char*>(Storage),9*Count*16);std::fflush(stdout);
    }
    Require(NegativeTotal>0,"Omitted visibility control was not detected");
    std::printf("RGB_TRACE_DECISION correctness=1 omitted_visibility_failures=%u production_promote=0\n",NegativeTotal);
}
}
