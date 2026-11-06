/*==========================================================================================================================================
                                                            SESSIONSPACE.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Session/SessionSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
constexpr float RadiansPerDegree = 0.017453292519943295f;
}

void SessionSpace::BeginPolicy(std::unique_ptr<SessionPolicy> Policy)
{
    ActivePolicy = std::move(Policy);
    if (ActivePolicy)
        ActivePolicy->OnSessionBegin(*this);
}

std::array<float, 3> SessionSpace::ResolveSlotPosition(uint32_t SlotIndex) const noexcept
{
    // 📝 The studio arrangement: the host at the centre, guests on the ring around them (as in the Lobby).
    if (SlotIndex == 0)
        return { 0.0f, 0.0f, 0.0f };
    const float Angle = (static_cast<float>(SlotIndex - 1) / 4.0f) * 360.0f + 45.0f;
    const float Radius = 10.5f;
    return { Radius * std::cos(Angle * RadiansPerDegree), Radius * std::sin(Angle * RadiansPerDegree), 0.0f };
}

float SessionSpace::ResolveSlotYawDegrees(uint32_t SlotIndex) const noexcept
{
    // Nose-in: every guest faces the centre car; the host faces +X.
    if (SlotIndex == 0)
        return 0.0f;
    const std::array<float, 3> Position = ResolveSlotPosition(SlotIndex);
    return std::atan2(-Position[1], -Position[0]) / RadiansPerDegree;
}

PlayerToken SessionSpace::AddPlayer(std::string_view NameTag, std::string_view Rank,
                                    std::string_view NetworkEndpoint, bool IsLocalHost)
{
    // 📝 Capacity is the assigned policy's answer first (the Lobby holds five); the configuration is
    //    only the fallback for policies that do not set one.
    const uint32_t Capacity = (ActivePolicy && ActivePolicy->QueryMaximumPlayers() > 0)
                                  ? ActivePolicy->QueryMaximumPlayers()
                                  : Configuration.MaximumPlayers;
    if (QueryConnectedCount() >= Capacity)
        return {};

    PlayerProfile Profile{};
    Profile.Token = PlayerToken{ NextPlayerValue++ };
    Profile.NameTag = std::string(NameTag);
    Profile.Rank = std::string(Rank);
    Profile.NetworkEndpoint = std::string(NetworkEndpoint);
    Profile.IsLocalHost = IsLocalHost;
    Players.push_back(std::move(Profile));
    const PlayerToken Token = Players.back().Token;

    auto Unit = std::make_unique<DeployableUnit>();
    if (ActivePolicy)
        Unit->AssignSpawnPolicy(ActivePolicy->QuerySpawnPolicy());
    const uint32_t Slot = QueryConnectedCount() - 1;
    Unit->Deploy(ResolveSlotPosition(Slot), ResolveSlotYawDegrees(Slot), SurfaceSampler);
    Units.push_back(std::move(Unit));

    auto NewAgent = std::make_unique<Agent>(Token, Units.back().get());
    Agents.push_back(std::move(NewAgent));

    auto Driver = std::make_unique<InputDriver>(Token);
    Driver->BindAgent(Agents.back().get());
    HumanDrivers.push_back(std::move(Driver));

    if (ActivePolicy)
        ActivePolicy->OnPlayerJoined(*this, Token);
    return Token;
}

PlayerToken SessionSpace::AddSyntheticPlayer(std::string_view NameTag, std::string_view Rank,
                                              const std::vector<SyntheticWaypoint>& Waypoints)
{
    const PlayerToken Token = AddPlayer(NameTag, Rank, "synthetic", false);
    if (!Token.IsValid())
        return {};
    if (PlayerProfile* Profile = FindPlayer(Token))
        Profile->SyntheticAi = true;

    // Hand the Agent from the human driver stand to a synthetic one on the given circuit.
    HumanDrivers.erase(std::remove_if(HumanDrivers.begin(), HumanDrivers.end(),
                                      [Token](const std::unique_ptr<InputDriver>& Driver) { return Driver->QueryPlayerToken() == Token; }),
                       HumanDrivers.end());
    auto Driver = std::make_unique<SyntheticDriver>(Token);
    Driver->ConfigureWaypoints(Waypoints);
    if (Agent* Found = FindAgent(Token))
        Driver->BindAgent(Found);
    SyntheticDrivers.push_back(std::move(Driver));
    return Token;
}

bool SessionSpace::RemovePlayer(PlayerToken Token)
{
    const auto Profile = std::find_if(Players.begin(), Players.end(),
                                      [Token](const PlayerProfile& Candidate) { return Candidate.Token == Token; });
    if (Profile == Players.end())
        return false;
    if (ActivePolicy)
        ActivePolicy->OnPlayerLeft(*this, Token);

    const auto EraseToken = [Token](const auto& Owner) { return Owner->QueryPlayerToken() == Token; };
    HumanDrivers.erase(std::remove_if(HumanDrivers.begin(), HumanDrivers.end(), EraseToken), HumanDrivers.end());
    SyntheticDrivers.erase(std::remove_if(SyntheticDrivers.begin(), SyntheticDrivers.end(), EraseToken), SyntheticDrivers.end());

    Agent* Found = FindAgent(Token);
    DeployableUnit* OwnedUnit = Found ? Found->QueryUnit() : nullptr;
    Agents.erase(std::remove_if(Agents.begin(), Agents.end(),
                                [Token](const std::unique_ptr<Agent>& Candidate) { return Candidate->QueryOwnerPlayerToken() == Token; }),
                 Agents.end());
    Units.erase(std::remove_if(Units.begin(), Units.end(),
                               [OwnedUnit](const std::unique_ptr<DeployableUnit>& Candidate) { return Candidate.get() == OwnedUnit; }),
                Units.end());
    Players.erase(Profile);
    return true;
}

void SessionSpace::SetPlayerReady(PlayerToken Token, bool Ready)
{
    if (PlayerProfile* Profile = FindPlayer(Token))
        Profile->Ready = Ready;
}

void SessionSpace::SubmitPlayerInput(PlayerToken Token, const InputPacket& Packet)
{
    const InputPacket Routed = ActivePolicy ? ActivePolicy->QueryInputRoutingPolicy().Route(Packet) : Packet;
    if (PlayerProfile* Profile = FindPlayer(Token))
        Profile->LatestInput = Routed;
    // Ready/reset are session actions, but only survive the active level's input-routing policy.
    if (Routed.HasAction(InputActionReady))
        SetPlayerReady(Token, true);
    if (Routed.HasAction(InputActionResetUnit))
        RequestAgentRespawn(Token);
}

void SessionSpace::StepSession(float StepSeconds)
{
    for (auto& Driver : SyntheticDrivers)
        Driver->StepDrive(StepSeconds);
    for (auto& Driver : HumanDrivers)
    {
        if (const PlayerProfile* Profile = FindPlayer(Driver->QueryPlayerToken()))
        {
            if (Agent* Found = FindAgent(Driver->QueryPlayerToken()))
                Found->ApplyInputPacket(Profile->LatestInput, StepSeconds);
        }
    }
    if (ActivePolicy)
        ActivePolicy->OnSessionStep(*this, StepSeconds);
}

bool SessionSpace::RequestAgentRespawn(PlayerToken Token)
{
    return ActivePolicy ? ActivePolicy->RequestAgentRespawn(*this, Token) : false;
}

PlayerProfile* SessionSpace::FindPlayer(PlayerToken Token) noexcept
{
    const auto Found = std::find_if(Players.begin(), Players.end(),
                                    [Token](const PlayerProfile& Candidate) { return Candidate.Token == Token; });
    return Found == Players.end() ? nullptr : &*Found;
}

const PlayerProfile* SessionSpace::FindPlayer(PlayerToken Token) const noexcept
{
    const auto Found = std::find_if(Players.begin(), Players.end(),
                                    [Token](const PlayerProfile& Candidate) { return Candidate.Token == Token; });
    return Found == Players.end() ? nullptr : &*Found;
}

Agent* SessionSpace::FindAgent(PlayerToken Token) noexcept
{
    const auto Found = std::find_if(Agents.begin(), Agents.end(),
                                    [Token](const std::unique_ptr<Agent>& Candidate) { return Candidate->QueryOwnerPlayerToken() == Token; });
    return Found == Agents.end() ? nullptr : Found->get();
}

DeployableUnit* SessionSpace::FindUnit(PlayerToken Token) noexcept
{
    Agent* Found = FindAgent(Token);
    return Found ? Found->QueryUnit() : nullptr;
}

const DeployableUnit* SessionSpace::FindUnit(PlayerToken Token) const noexcept
{
    const auto Found = std::find_if(Agents.begin(), Agents.end(),
                                    [Token](const std::unique_ptr<Agent>& Candidate) { return Candidate->QueryOwnerPlayerToken() == Token; });
    return Found == Agents.end() ? nullptr : (*Found)->QueryUnit();
}

bool SessionSpace::AllPlayersReady() const noexcept
{
    return QueryConnectedCount() > 0 &&
           std::all_of(Players.begin(), Players.end(), [](const PlayerProfile& Profile) { return !Profile.Connected || Profile.Ready; });
}

uint32_t SessionSpace::QueryConnectedCount() const noexcept
{
    return static_cast<uint32_t>(std::count_if(Players.begin(), Players.end(),
                                               [](const PlayerProfile& Profile) { return Profile.Connected; }));
}

} // namespace Frontier
