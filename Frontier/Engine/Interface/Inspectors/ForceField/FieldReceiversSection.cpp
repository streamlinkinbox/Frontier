/*==========================================================================================================================================
                                                        FIELDRECEIVERSSECTION.CPP
==========================================================================================================================================*/
// 🧩 Receivers: who responds to the field (its receiver list; each force may narrow it with Acts on), how many forces reach each
//    receiver, and where the Wind carries the weather media (its advection switches and the cloud and fog links).

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"

#include <bit>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

// 📝 A link the receiver's own record keeps (FollowWind); it advances that record's revision.
void ConstructLink(ControlColumn& Page, const char* Id, const char* Label, bool* Follow, RevisionCounter* Owner, const char* Tooltip)
{
    bool Unused = false;
    if (SectionRows::Switch(Page, Id, Label, Follow != nullptr ? *Follow : Unused, Follow == nullptr ? "Not in the level" : nullptr, Tooltip) &&
        Owner != nullptr)
        Owner->AdvanceRevision();
}

} // namespace

bool FieldReceiversSection::Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading)
{
    using namespace SectionRows;
    const auto Receivers = EnumerateForceReceivers();
    char       Status[32];
    std::snprintf(Status, sizeof(Status), "%d of %zu", std::popcount(Field.Receivers & ForceReceiverEvery), Receivers.size());
    BeginSection(Page, "Receivers", "Sparkles", Status,
                 "Who responds is configuration, not code: the field names its receivers, each force may narrow the list (Acts on), "
                 "and a receiver outside it ignores the field entirely.");
    bool Changed = false;
    SubHeading(Page, "Receivers that respond to the field");
    for (const ForceReceiverDescriptor& Receiver : Receivers)
    {
        bool       Responds = (Field.Receivers & Receiver.Receiver) != 0u;
        const auto Id       = std::string("FieldReceiver") + Receiver.Identifier;
        if (Switch(Page, Id.c_str(), Receiver.Label, Responds))
        {
            Field.Receivers = Responds ? (Field.Receivers | Receiver.Receiver) : (Field.Receivers & ~static_cast<uint32_t>(Receiver.Receiver));
            Changed         = true;
        }
    }
    // 📝 How many enabled forces reach each of the first six receivers (off when the field leaves it out).
    std::vector<MetricTile> Tiles;
    std::vector<std::string> Labels;
    for (size_t Index = 0; Index < Receivers.size() && Index < 6; ++Index)
    {
        const ForceReceiverDescriptor& Receiver = Receivers[Index];
        uint32_t                       Reached  = 0;
        for (const ForceSourceConfiguration& Source : Reading.Sources)
            Reached += Source.Enabled && (Source.ActsOn == 0u || (Source.ActsOn & Receiver.Receiver) != 0u) ? 1u : 0u;
        const bool Responds = (Field.Receivers & Receiver.Receiver) != 0u;
        Tiles.push_back({Receiver.Label, Responds ? std::to_string(Reached) : std::string("off"), Responds ? "forces" : ""});
    }
    for (size_t Row = 0; Row < Tiles.size(); Row += 3)
        MetricTiles::Construct(Page, Tiles.data() + Row, static_cast<int>(std::min<size_t>(3, Tiles.size() - Row)), MetricTiles::NoLead);
    if ((Field.Receivers & ForceReceiverEvery) == 0u)
        CaptionText::Construct(Page, "No receiver responds: the field is authored but moves nothing.", 2.0f, 8.0f);

    SubHeading(Page, "Wind receivers \xC2\xB7 WindRecord");
    WindConfiguration* Wind = Reading.Access != nullptr ? Reading.Access->Wind : nullptr;
    bool               Unused = false;
    const char*        Absent = Wind == nullptr ? "Add a Wind record first" : nullptr;
    bool               Edited = false;
    Edited |= Switch(Page, "WindAdvectClouds", "Advect clouds", Wind != nullptr ? Wind->AdvectClouds : Unused, Absent,
                     "Cloud density travels with the wind at cloud-base altitude");
    Edited |= Switch(Page, "WindAdvectPlumes", "Advect plumes", Wind != nullptr ? Wind->AdvectPlumes : Unused, Absent,
                     "Smoke and steam plumes lean and drift");
    Edited |= Switch(Page, "WindAdvectParticles", "Advect particles", Wind != nullptr ? Wind->AdvectParticles : Unused, Absent,
                     "Particle effects with wind coupling follow the field");
    if (Edited && Wind != nullptr) Wind->AdvanceRevision();
    const EnvironmentEditAccess* Access = Reading.Access;
    ConstructLink(Page, "LinkClouds", "Clouds follow the wind", Access != nullptr && Access->Clouds ? &Access->Clouds->FollowWind : nullptr,
                  Access != nullptr ? Access->Clouds : nullptr, "Density travels at cloud-base altitude; linking does not enable the layer");
    ConstructLink(Page, "LinkLocalCloud", "Local cloud follows the wind",
                  Access != nullptr && Access->LocalCloud ? &Access->LocalCloud->FollowWind : nullptr, Access != nullptr ? Access->LocalCloud : nullptr,
                  "Placed bounds stay fixed; density travels");
    ConstructLink(Page, "LinkLocalFog", "Local fog follows the wind", Access != nullptr && Access->LocalFog ? &Access->LocalFog->FollowWind : nullptr,
                  Access != nullptr ? Access->LocalFog : nullptr, "Unlinked, it keeps its own authored drift");
    CaptionText::Construct(Page, "Static height fog has no advected density. Vegetation and precipitation consumers are not registered here; "
                                 "their response is not claimed.",
                           4.0f, 8.0f);
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
