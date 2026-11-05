/*==========================================================================================================================================
                                                            LOBBYSESSIONMAIN.CPP
==========================================================================================================================================*/
// 🧩 The game, headless: opens on the Lobby (the default level), joins the local player under the Lobby
//    policy, steps the session, exercises the debug level switch, and — with --render — draws the studio
//    through the software presentation so the C++ Lobby can be seen without a GPU.
//
//      LobbySession [--archive DIR] [--render out.ppm] [--guests N] [--xray]
//
//    The facet archives are the vehicle's own (Experimental/VehicleHmiPrototype/Archive by default).

#include "Frontier/Engine/Session/LobbySession.h"
#include "Frontier/Game/SoftwarePresentation.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;

    std::string ArchiveDirectory = "Experimental/VehicleHmiPrototype/Archive";
    std::string RenderPath;
    int Guests = 0;
    bool XRay = false;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        if (std::strcmp(Arguments[Index], "--archive") == 0 && Index + 1 < ArgumentCount)
            ArchiveDirectory = Arguments[++Index];
        else if (std::strcmp(Arguments[Index], "--render") == 0 && Index + 1 < ArgumentCount)
            RenderPath = Arguments[++Index];
        else if (std::strcmp(Arguments[Index], "--xray") == 0)
            XRay = true;
        else if (std::strcmp(Arguments[Index], "--guests") == 0 && Index + 1 < ArgumentCount)
            Guests = std::atoi(Arguments[++Index]);
    }

    LobbySession Game;
    const SessionSpace& Session = Game.QuerySession();
    std::printf("level: %s (policy: %s)\n", Game.QueryCurrentLevelPath().c_str(),
                Session.QueryPolicy() ? Session.QueryPolicy()->QueryPolicyName().data() : "none");
    std::printf("policy: maximumPlayers=%u respawn=%s derez=%s\n",
                Session.QueryPolicy() ? Session.QueryPolicy()->QueryMaximumPlayers() : 0,
                Session.QueryPolicy() && Session.QueryPolicy()->QueryRespawnPolicy().IsRespawnAllowed() ? "allowed" : "disabled",
                Session.QueryPolicy() && Session.QueryPolicy()->AllowsDerez() ? "allowed" : "disabled");

    // Fill the studio from the same field as the prototype: guests join until the policy says full.
    static const char* Names[4] = { "KESTREL", "MIRAGE", "ORION", "VEGA-9" };
    static const char* Ranks[4] = { "DIAMOND I", "SILVER III", "BRONZE I", "GOLD III" };
    for (int Guest = 0; Guest < Guests && Guest < 4; ++Guest)
        Game.AccessSession().AddPlayer(Names[Guest], Ranks[Guest]);
    const PlayerToken Extra = Game.AccessSession().AddPlayer("EXTRA", "BRONZE I");
    std::printf("players: %u (a sixth join %s — the policy holds five)\n", Session.QueryConnectedCount(),
                Extra.IsValid() ? "was ACCEPTED" : "was refused");

    for (int Step = 0; Step < 120; ++Step)
        Game.Step(1.0f / 60.0f);

    if (DeployableUnit* Vehicle = Game.QueryLocalVehicle())
    {
        const std::array<float, 3>& Position = Vehicle->QueryPositionMetres();
        std::printf("vehicle: stand (%.2f, %.2f, %.2f) yaw %.1f, handbrake latched: %s\n",
                    Position[0], Position[1], Position[2], Vehicle->QueryYawDegrees(),
                    Vehicle->IsHandbrakeLatched() ? "yes (parked)" : "no");
        std::printf("cabin: steering wheel %.1f deg, tablet powered: %s, content: %s\n",
                    Vehicle->QueryCabin().Wheel.AngleDegrees,
                    Vehicle->QueryCabin().Tablet.Powered ? "yes" : "no",
                    Vehicle->QueryCabin().Tablet.IsEmpty() ? "empty (no HMI yet)" : "showing");
    }

    // Driving works even parked-awareness: throttle releases the latch, the wheel follows the steer axis.
    InputPacket Drive{};
    Drive.ThrottleAxis = 1.0f;
    Drive.SteerAxis = 0.5f;
    for (int Step = 0; Step < 60; ++Step)
    {
        Game.SubmitLocalInput(Drive);
        Game.Step(1.0f / 60.0f);
    }
    if (DeployableUnit* Vehicle = Game.QueryLocalVehicle())
        std::printf("after 1 s of throttle: speed %.2f m/s, wheel %.1f deg, handbrake latched: %s\n",
                    Vehicle->QueryForwardSpeedMps(), Vehicle->QueryCabin().Wheel.AngleDegrees,
                    Vehicle->IsHandbrakeLatched() ? "yes" : "no (released)");

#if FRONTIER_DEBUG_LEVEL_SWITCH
    Game.HandleDebugKeys(true, true, true, false, -1);   // Ctrl+Shift+L opens the popup
    std::printf("debug popup (Ctrl+Shift+L): %s\n", Game.IsDebugPopupOpen() ? "open" : "closed");
    Game.HandleDebugKeys(false, false, false, false, 3); // digit 3: Proving Grounds under RacePolicy
    std::printf("after debug switch: level %s, policy %s, players preserved: %zu\n",
                Game.QueryCurrentLevelPath().c_str(),
                Game.QuerySession().QueryPolicy() ? Game.QuerySession().QueryPolicy()->QueryPolicyName().data() : "none",
                Game.QuerySession().QueryPlayers().size());
#endif

    if (!RenderPath.empty())
    {
        const FacetArchiveData Body = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleBody.facet");
        const FacetArchiveData Tyre = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleTyre.facet");
        const FacetArchiveData Rim = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleRim.facet");
        if (!Body.IsValid())
        {
            std::printf("render: vehicle archives not found under %s\n", ArchiveDirectory.c_str());
            return 1;
        }
        // Return to the lobby for the portrait and stand the fleet as the session placed it.
#if FRONTIER_DEBUG_LEVEL_SWITCH
        Game.SwitchLevelForDebug(0);
#endif
        std::vector<PresentationVehicle> Vehicles;
        static const std::array<float, 3> Paints[5] = {
            { 0.20f, 0.21f, 0.23f }, { 0.35f, 0.55f, 0.42f }, { 0.55f, 0.45f, 0.28f },
            { 0.38f, 0.48f, 0.62f }, { 0.58f, 0.40f, 0.36f } };
        size_t Index = 0;
        for (const PlayerProfile& Profile : Game.QuerySession().QueryPlayers())
        {
            (void)Profile;
            if (const DeployableUnit* Unit = Game.QuerySession().FindUnit(Profile.Token))
            {
                PresentationVehicle Vehicle{};
                Vehicle.Body = &Body;
                Vehicle.Tyre = &Tyre;
                Vehicle.Rim = &Rim;
                Vehicle.PositionMetres = Unit->QueryPositionMetres();
                Vehicle.YawDegrees = Unit->QueryYawDegrees();
                Vehicle.Paint = Paints[Index % 5];
                if (XRay && Index == 0)
                {
                    Vehicle.Inspection.View = InspectionView::Scan;
                    Vehicle.Inspection.ScanSeconds = 1.6f;   // band mid-car for the still
                }
                Vehicles.push_back(Vehicle);
            }
            ++Index;
        }
        if (!RenderLobbyPpm(RenderPath, Vehicles, 38.0f, 14.0f, 15.5f))
        {
            std::printf("render: could not write %s\n", RenderPath.c_str());
            return 1;
        }
        std::printf("render: %s (%zu vehicles, body %u facets)\n", RenderPath.c_str(), Vehicles.size(), Body.FacetCount);
    }
    return 0;
}
