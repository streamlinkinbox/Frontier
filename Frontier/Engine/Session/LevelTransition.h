#pragma once
/*==========================================================================================================================================
                                                            LEVELTRANSITION.H
==========================================================================================================================================*/
// 🧩 Leaving one level for another without losing the session: captures a TransferConfiguration from the
//    SessionSpace, asks the active policy's rules whether the session may leave (everyone ready in the
//    Lobby, a finished race elsewhere), then rebuilds the room on the far side — same players, same cars,
//    fresh units deployed under the new policy's spawn rules.

#include "Frontier/Engine/Session/SessionSpace.h"
#include "Frontier/Engine/Session/TransferConfiguration.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Frontier
{

class LevelTransition
{
  public:
    using PolicyFactory = std::function<std::unique_ptr<SessionPolicy>(std::string_view PolicyName)>;

    LevelTransition() = default;
    explicit LevelTransition(PolicyFactory InFactory) : Factory(std::move(InFactory)) {}
    void ConfigurePolicyFactory(PolicyFactory InFactory) { Factory = std::move(InFactory); }

    // 📝 If the active policy's rules say the session may leave now, performs the transition and returns
    //    true. The transfer is kept either way it was last captured, for inspection and replication.
    bool TryTransition(SessionSpace& Session);

    // 📝 A deliberate navigation-menu route. It uses the same profile-preserving rebuild as a policy-driven
    //    transition, but the destination is selected by the app rather than the active policy countdown.
    bool TryTransitionTo(SessionSpace& Session, std::string TargetLevelPath, std::string TargetPolicyName);

    [[nodiscard]] const TransferConfiguration& QueryLastTransfer() const noexcept { return LastTransfer; }

  private:
    PolicyFactory Factory{};
    TransferConfiguration LastTransfer{};
};

} // namespace Frontier
