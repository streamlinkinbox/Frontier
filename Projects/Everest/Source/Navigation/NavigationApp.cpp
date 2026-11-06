/*==========================================================================================================================================
                                                           NAVIGATIONAPP.CPP
==========================================================================================================================================*/

#include "Projects/Everest/Source/Navigation/NavigationApp.h"

#include <ostream>

namespace Everest
{

NavigationApp::NavigationApp(const Frontier::SessionConfiguration& Configuration)
    : Destinations{
          { "lobby", "Lobby", Configuration.LobbyLevelPath, "EverestLobbyPolicy" },
          { "proving-grounds", "Proving Grounds", Configuration.MatchLevelPath, "EverestProvingGroundsPolicy" }
      }
{
}

bool NavigationApp::IsCurrent(const Frontier::LobbySession& Session, const NavigationDestination& Destination) const
{
    return Session.QueryCurrentLevelPath() == Destination.LevelPath;
}

bool NavigationApp::Navigate(Frontier::LobbySession& Session, std::string_view Selection) const
{
    for (const NavigationDestination& Destination : Destinations)
        if (Destination.Id == Selection || Destination.Label == Selection)
            return Session.NavigateToLevel(Destination.LevelPath, Destination.PolicyName);
    return false;
}

bool NavigationApp::Navigate(Frontier::LobbySession& Session, size_t OneBasedIndex) const
{
    if (OneBasedIndex == 0 || OneBasedIndex > Destinations.size())
        return false;
    const NavigationDestination& Destination = Destinations[OneBasedIndex - 1];
    return Session.NavigateToLevel(Destination.LevelPath, Destination.PolicyName);
}

void NavigationApp::PrintMenu(std::ostream& Output, const Frontier::LobbySession& Session) const
{
    Output << "Navigation\n";
    for (size_t Index = 0; Index < Destinations.size(); ++Index)
    {
        const NavigationDestination& Destination = Destinations[Index];
        Output << (Index + 1) << ". " << Destination.Label;
        if (IsCurrent(Session, Destination))
            Output << "  [current]";
        Output << "  -> " << Destination.LevelPath << " (" << Destination.PolicyName << ")\n";
    }
}

} // namespace Everest
