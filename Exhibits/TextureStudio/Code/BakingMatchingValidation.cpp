/*============================================================================================================================================
                                                   BAKINGMATCHINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validate per-target CPU/GPU routing using the real panel state machine and a CPU field-backed GPU hook.
#include "UI/TextureStudio/BakingWorkspacePanel.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace Frontier
{
struct BakingWorkspaceValidation
{
    static void Setup(BakingWorkspacePanel &Panel)
    {
        auto Cube = BuildDemoBakeAssets("Cube");
        Panel.Assets.Low = Cube.Low; Panel.Assets.High = Cube.High;
        Panel.Assets.Low.Parts[0].Name = "Alpha_low";
        Panel.Assets.High.Parts[0].Name = "Alpha_high";
        auto Low = Panel.Assets.Low.Parts[0]; Low.Name = "Beta_low";
        auto High = Panel.Assets.High.Parts[0]; High.Name = "Beta_high";
        // Move the second high source away: if matching fails the second target would falsely hit Alpha.
        for (auto &V : High.Vertices) V.Position[2] += 20.f;
        Panel.Assets.Low.Parts.push_back(Low); Panel.Assets.High.Parts.push_back(High);
        Panel.SelectedTargets = {"Alpha_low", "Beta_low"};
        Panel.SelectedSources = {"Alpha_high", "Beta_high"};
        Panel.Settings.Resolution = 24; Panel.Settings.Padding = 0; Panel.Settings.Channels = {"normal", "alpha"};
        Panel.Settings.MatchByName = true; Panel.PreferGpuBake = true;
    }
    static void Start(BakingWorkspacePanel &Panel) { Panel.StartBake(); }
    static bool Running(const BakingWorkspacePanel &Panel) { return Panel.BakeRunning.load() || Panel.VolumeRunning; }
    static void QueueVolume(BakingWorkspacePanel &Panel) { Panel.VolumeQueued=true; Panel.VolumeDimensions={8,8,8}; }
    static const auto &Results(const BakingWorkspacePanel &Panel) { return Panel.Results; }
    static const auto &Error(const BakingWorkspacePanel &Panel) { return Panel.Error; }
    static const auto &Device(const BakingWorkspacePanel &Panel) { return Panel.BakeDeviceUsed; }
};
}
int main()
{
    using namespace Frontier;
    try
    {
        ActivationOwnership Ownership(new BakingWorkspacePanel());
        auto &Panel = *static_cast<BakingWorkspacePanel *>(Ownership.get());
        if (!Panel.Initialize() || !Panel.Activate()) throw std::runtime_error("Panel activation failed.");
        BakingWorkspaceValidation::Setup(Panel);
        std::shared_ptr<const BakeFieldRequest> Active;
        int Submissions=0; bool FailSecond=false;
        Panel.QueryGpuBakeDevice = [] { return std::string("Test field device"); };
        Panel.SubmitGpuBake = [&](std::shared_ptr<const BakeFieldRequest> Request)
        {
            if (Request->Targets.size()!=1) throw std::runtime_error("Expected one matched target per GPU request.");
            float MinimumZ = Request->Triangles.BoundsMinimum[2];
            if ((Submissions%2 == 0 && MinimumZ > 10.f) || (Submissions%2 == 1 && MinimumZ < 10.f))
                throw std::runtime_error("GPU request contains the wrong high source.");
            Active=std::move(Request); ++Submissions; return true;
        };
        Panel.PollGpuBake = [&]
        {
            BakeGpuOutcome Outcome; Outcome.Complete=true;
            if (FailSecond && Submissions%2==0) { Outcome.Failed=true; Outcome.Failure="synthetic GPU failure"; return Outcome; }
            Outcome.Fields.resize(1); EvaluateBakeField(*Active, 0, Outcome.Fields[0]);
            Outcome.DoneTexels=Outcome.TotalTexels=Outcome.Fields[0].size(); Active.reset(); return Outcome;
        };
        auto Run = [&]
        {
            BakingWorkspaceValidation::Start(Panel);
            for (int Guard=0; Guard<5000 && BakingWorkspaceValidation::Running(Panel); ++Guard)
            { (void)Panel.Integrate(1.0/60.0); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            if (BakingWorkspaceValidation::Running(Panel) || !BakingWorkspaceValidation::Error(Panel).empty()) throw std::runtime_error("Bake did not finish: " + BakingWorkspaceValidation::Error(Panel));
            const auto &Results=BakingWorkspaceValidation::Results(Panel);
            if (Results.size()!=2 || Results[0].Name!="Alpha_low" || Results[1].Name!="Beta_low" || !Results[0].Stats.Hits || Results[1].Stats.Hits)
                throw std::runtime_error("Per-target matching returned incorrect results.");
            return Results;
        };
        auto GpuResults=Run();
        if (Submissions!=2 || BakingWorkspaceValidation::Device(Panel)!="Test field device") throw std::runtime_error("GPU matched sequence did not complete.");
        FailSecond=true;
        auto CpuResults=Run();
        if (Submissions!=4 || BakingWorkspaceValidation::Device(Panel).find("CPU after GPU refusal")!=0) throw std::runtime_error("GPU fallback did not restart the full CPU bake.");
        for (size_t I=0; I<GpuResults.size(); ++I) for (size_t M=0; M<GpuResults[I].Maps.size(); ++M)
            if (GpuResults[I].Maps[M].Rgba!=CpuResults[I].Maps[M].Rgba) throw std::runtime_error("GPU field and CPU fallback differ.");
        FailSecond=false; BakingWorkspaceValidation::QueueVolume(Panel);
        (void)Run();
        if (Submissions!=6 || Panel.QueryVolume().Distances.size()!=512) throw std::runtime_error("Volume and maps batch did not finish.");
        Ownership.reset();
        // A vertical high triangle cannot be hit by the low plane's normal rays, but is nearby.
        BakePart Low; Low.Name = "Plane_low";
        for (int I=0; I<3; ++I) { BakeVertex V; V.Position[0]=I==1 ? 1.f : 0.f; V.Position[1]=I==2 ? 1.f : 0.f; V.Uv[0]=V.Position[0]; V.Uv[1]=V.Position[1]; V.ChartId=1; Low.Vertices.push_back(V); }
        Low.Indices={0,1,2};
        BakePart High; High.Name="Plane_high";
        for (int I=0; I<3; ++I) { BakeVertex V; V.Position[0]=.5f; V.Position[1]=I==1 ? 1.f : 0.f; V.Position[2]=I==2 ? 1.f : -1.f; V.Normal[0]=1; V.Normal[2]=0; High.Vertices.push_back(V); }
        High.Indices={0,1,2}; BakeMeshSet Source; Source.Parts.push_back(High);
        BakeSettings Settings; Settings.Resolution=16; Settings.Channels={"alpha"}; Settings.Front=Settings.Back=.2f;
        const auto Ray=RunMeshBake({&Low}, Source, Settings);
        Settings.ProjectionRay=false;
        const auto Nearest=RunMeshBake({&Low}, Source, Settings);
        if (Ray[0].Stats.Hits != 0 || Nearest[0].Stats.Hits == 0) throw std::runtime_error("Nearest surface projection does not differ from normal rays.");
        std::cout << "PASS: multi-target matched GPU routing, CPU fallback, volume batch and nearest-surface projection\n";
    }
    catch (const std::exception &Failure) { std::cerr << Failure.what() << '\n'; return 1; }
}
