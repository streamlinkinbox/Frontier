/*==========================================================================================================================================
                                                               EVERESTMAIN.CPP
==========================================================================================================================================*/
// 🧩 Project Everest's standalone C++ game entry point. It opens on the Lobby, drives the two-level
//    NavigationApp, and can render either level with the same ControlVehicle facet archives.
//
//      Everest [--navigate lobby|proving-grounds ...] [--menu] [--guests N]
//              [--render out.ppm] [--archive DIR] [--xray]

#include "Frontier/Engine/Session/LobbySession.h"
#include "Frontier/Game/SoftwarePresentation.h"
#include "Projects/Everest/Source/Navigation/NavigationApp.h"
#include "Projects/Everest/Source/Session/SessionPolicy.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;

    SessionConfiguration Configuration{};
    std::string ArchiveDirectory = "Experimental/VehicleHmiPrototype/Archive";
    std::string RenderPath;
    std::vector<std::string> NavigationRequests;
    int Guests = 0;
    bool XRay = false;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        if (std::strcmp(Arguments[Index], "--navigate") == 0 && Index + 1 < ArgumentCount)
            NavigationRequests.emplace_back(Arguments[++Index]);
        else if (std::strcmp(Arguments[Index], "--render") == 0 && Index + 1 < ArgumentCount)
            RenderPath = Arguments[++Index];
        else if (std::strcmp(Arguments[Index], "--archive") == 0 && Index + 1 < ArgumentCount)
            ArchiveDirectory = Arguments[++Index];
        else if (std::strcmp(Arguments[Index], "--guests") == 0 && Index + 1 < ArgumentCount)
            Guests = std::max(0, std::atoi(Arguments[++Index]));
        else if (std::strcmp(Arguments[Index], "--xray") == 0)
            XRay = true;
    }

    LobbySession Game(Configuration, Everest::ConstructEverestSessionPolicy);
    Everest::NavigationApp Navigation(Configuration);
    static const char* Names[4] = { "KESTREL", "MIRAGE", "ORION", "VEGA-9" };
    static const char* Ranks[4] = { "DIAMOND I", "SILVER III", "BRONZE I", "GOLD III" };
    for (int Guest = 0; Guest < Guests && Guest < 4; ++Guest)
        Game.AccessSession().AddPlayer(Names[Guest], Ranks[Guest]);

    for (const std::string& Request : NavigationRequests)
        if (!Navigation.Navigate(Game, Request))
        {
            std::fprintf(stderr, "navigation: unknown destination or already there: %s\n", Request.c_str());
            Navigation.PrintMenu(std::cerr, Game);
            return 2;
        }

    Navigation.PrintMenu(std::cout, Game);
    const SessionSpace& Session = Game.QuerySession();
    std::printf("Project Everest level: %s\n", Game.QueryCurrentLevelPath().c_str());
    std::printf("policy: %s; vehicles: %zu; transition epoch: %llu\n",
                Session.QueryPolicy() ? Session.QueryPolicy()->QueryPolicyName().data() : "none",
                Session.QueryPlayers().size(), static_cast<unsigned long long>(Session.QueryTransitionEpoch()));
    if (const DeployableUnit* Vehicle = Game.QueryLocalVehicle())
    {
        const auto& Position = Vehicle->QueryPositionMetres();
        std::printf("local vehicle: (%.2f, %.2f, %.2f), handbrake %s, tablet %s\n",
                    Position[0], Position[1], Position[2], Vehicle->IsHandbrakeLatched() ? "latched" : "released",
                    Vehicle->QueryCabin().Tablet.Powered ? "powered (empty; no HMI)" : "off");
    }

    if (!RenderPath.empty())
    {
        const FacetArchiveData Body = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleBody.facet");
        const FacetArchiveData Tyre = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleTyre.facet");
        const FacetArchiveData Rim = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleRim.facet");
        if (!Body.IsValid() || !Tyre.IsValid() || !Rim.IsValid())
        {
            std::fprintf(stderr, "render: vehicle facet archives not found or invalid under %s\n", ArchiveDirectory.c_str());
            return 1;
        }

        static const std::array<std::array<float, 3>, 5> Paints = {{
            {{ 0.20f, 0.21f, 0.23f }}, {{ 0.35f, 0.55f, 0.42f }}, {{ 0.55f, 0.45f, 0.28f }},
            {{ 0.38f, 0.48f, 0.62f }}, {{ 0.58f, 0.40f, 0.36f }}
        }};
        std::vector<PresentationVehicle> Vehicles;
        size_t Index = 0;
        for (const PlayerProfile& Profile : Session.QueryPlayers())
        {
            const DeployableUnit* Unit = Session.FindUnit(Profile.Token);
            if (!Unit)
                continue;
            PresentationVehicle Vehicle{};
            Vehicle.Body = &Body;
            Vehicle.Tyre = &Tyre;
            Vehicle.Rim = &Rim;
            Vehicle.PositionMetres = Unit->QueryPositionMetres();
            Vehicle.YawDegrees = Unit->QueryYawDegrees();
            Vehicle.Paint = Paints[Index % Paints.size()];
            if (XRay && Profile.IsLocalHost)
            {
                Vehicle.Inspection.View = InspectionView::Scan;
                Vehicle.Inspection.ScanSeconds = 1.6f;
            }
            Vehicles.push_back(Vehicle);
            ++Index;
        }

        const bool IsProvingGrounds = Game.QueryCurrentLevelPath() == Configuration.MatchLevelPath;
        const bool Written = IsProvingGrounds
            ? RenderProvingGroundsPpm(RenderPath, Vehicles, 38.0f, 14.0f, 16.5f)
            : RenderLobbyPpm(RenderPath, Vehicles, 38.0f, 14.0f, 15.5f);
        if (!Written)
        {
            std::fprintf(stderr, "render: could not write %s\n", RenderPath.c_str());
            return 1;
        }
        std::printf("render: %s (%s level, %zu vehicles, body %u facets)\n", RenderPath.c_str(),
                    IsProvingGrounds ? "Proving Grounds" : "Lobby", Vehicles.size(), Body.FacetCount);
    }
    return 0;
}
