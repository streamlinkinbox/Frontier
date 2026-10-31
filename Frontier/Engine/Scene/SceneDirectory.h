/*==========================================================================================================================================
                                                             SCENE DIRECTORY
                                     ------------------------------------------------------------------
          Editor-machinery Outliner model: names, folder enclosures, and inspector descriptions, Editor/Development only.
==========================================================================================================================================*/

// 🧩 Editor-machinery Outliner model: names, folder enclosures, and inspector descriptions, Editor/Development only.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "RecordClassification.h"
#include "RecordEditorDescription.h"
#include "RecordToken.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier
{

class RecordEntry;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE DIRECTORY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The Outliner ("SceneDirectory" in code per plan D2): directory-side metadata for records — display name,
//    folder/enclosure nesting, visibility, accent colour, and editor inspector description — while identity itself
//    lives in the RecordStore. RecordEntry's constructor/destructor drive Register/Unregister under Editor/Development
//    profiles only; Shipping compiles those calls out and its builds do not link SceneDirectory.cpp, so Shipping
//    binaries contain no directory at all (plan §5/§6).
class SceneDirectory
{
  public:
    [[nodiscard]] static SceneDirectory& Instance();

    SceneDirectory()                                 = default;
    SceneDirectory(const SceneDirectory&)            = delete;
    SceneDirectory& operator=(const SceneDirectory&) = delete;

    // Called only by RecordEntry's constructor (Editor/Development). Re-registering a token is refused (logged).
    void Register(const RecordEntry& Entry);

    // Called only by RecordEntry's destructor. Enclosed records under the removed entry re-root cleanly.
    void Unregister(RecordToken Token);

    // Directory-side metadata. Both return false (logged) for unknown tokens; ConfigureParent also refuses
    // self-parenting and cycles. Passing an invalid token makes the target a root entry.
    bool ConfigureName(RecordToken Token, std::string Name);
    bool ConfigureParent(RecordToken Target, RecordToken Enclosure);

    // Outliner visibility (the VehicleHMI rows' eye toggle): records start visible; hidden records stay registered.
    // ConfigureCascadeVisible propagates visibility across the entire enclosed subtree (used by Folder toggles).
    bool               ConfigureVisible(RecordToken Token, bool Visible);
    bool               ConfigureCascadeVisible(RecordToken Token, bool Visible);
    [[nodiscard]] bool QueryVisible(RecordToken Token) const;   // [-] - False for unknown tokens

    // Optional row/folder accent colour (packed RGBA8; 0 = classification default).
    bool                   ConfigureAccentColour(RecordToken Token, uint32_t PackedRgba);
    [[nodiscard]] uint32_t QueryAccentColour(RecordToken Token) const;

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    bool                                         ConfigureEditorDescription(RecordToken Token, RecordEditorDescription Description);
    [[nodiscard]] const RecordEditorDescription* QueryEditorDescription(RecordToken Token) const;
    [[nodiscard]] std::string                    QueryLiveBadge(RecordToken Token) const;
    [[nodiscard]] RecordToken                    FindTokenByIdentifier(std::string_view Identifier) const;
    void                                         SynchronizeEnabledFromDescriptions();
#endif

    [[nodiscard]] bool                 QueryRegistered(RecordToken Token) const;
    [[nodiscard]] std::string          QueryName(RecordToken Token) const;        // [text] - Configured name, else "<Classification> <token>"
    [[nodiscard]] RecordToken          QueryParent(RecordToken Token) const;      // [-] - Invalid token for roots/unknown
    [[nodiscard]] RecordClassification QueryClassification(RecordToken Token) const;
    [[nodiscard]] uint32_t             QueryChildCount(RecordToken Token) const;  // [records]
    [[nodiscard]] size_t               QueryCount() const;                        // [records]

    // Registration order snapshot (oldest first) for panel projections; copied under lock.
    void CopyRegistrationOrder(std::vector<RecordToken>& Out) const;

  private:
    struct DirectoryEntry
    {
        std::string          Name;                                                     // [text] - Empty means default name
        RecordToken          Enclosure;                                                // [-] - Invalid token = root
        RecordClassification Classification = RecordClassification::Unclassified;
        uint32_t             AccentColour   = 0;                                       // [RGBA8] - 0 = classification default
        bool                 Visible        = true;                                    // [-] - Outliner eye toggle
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        std::optional<RecordEditorDescription> Description;
#endif
    };

    mutable std::mutex                           Mutex;
    std::vector<RecordToken>                     RegistrationOrder;   // [-] - Oldest registration first
    std::unordered_map<uint64_t, DirectoryEntry> Entries;             // [-] - Token value -> directory metadata
};

} // namespace Frontier
