#pragma once
/*==========================================================================================================================================
                                                           NAVIGATIONAPP.H
==========================================================================================================================================*/
// 🧩 Everest's level-navigation app: a deliberately small menu that can route between the Lobby and
//    Proving Grounds in either direction without depending on the vehicle HMI.

#include "Frontier/Engine/Session/LobbySession.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace Everest
{

struct NavigationDestination
{
    std::string Id;
    std::string Label;
    std::string LevelPath;
    std::string PolicyName;
};

class NavigationApp
{
  public:
    explicit NavigationApp(const Frontier::SessionConfiguration& Configuration = {});

    [[nodiscard]] const std::vector<NavigationDestination>& QueryDestinations() const noexcept { return Destinations; }
    [[nodiscard]] bool IsCurrent(const Frontier::LobbySession& Session, const NavigationDestination& Destination) const;

    // 📝 Selects by menu id or one-based menu index. The Lobby is option 1; the proving grounds is option 2.
    bool Navigate(Frontier::LobbySession& Session, std::string_view Selection) const;
    bool Navigate(Frontier::LobbySession& Session, size_t OneBasedIndex) const;
    void PrintMenu(std::ostream& Output, const Frontier::LobbySession& Session) const;

  private:
    std::vector<NavigationDestination> Destinations;
};

} // namespace Everest
