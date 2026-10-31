/*==========================================================================================================================================
                                                               RECORD ENTRY
                                     ------------------------------------------------------------------
          Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.
==========================================================================================================================================*/

// 🧩 Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "RecordClassification.h"
#include "RecordEditorDescription.h"
#include "RecordToken.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD ENTRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Plan §6: construction issues a token, inserts into the RecordStore (every profile) and — Editor/Development
//    only — registers with the SceneDirectory so the object appears in the Outliner. Destruction reverses both.
//    Forgetting to register is impossible; there is no other registration API. Display name, Outliner nesting and
//    Inspector description are directory-side metadata bound in the derived constructor and reclaimed on destruction.
//    ⚠️ Records are constructed on the primary thread in editor builds; job threads never construct a RecordEntry.
//    ⚠️ Records must not have static storage duration (the store/directory are function-local statics).
class RecordEntry
{
  public:
    explicit RecordEntry(RecordClassification Classification);
    virtual ~RecordEntry();

    RecordEntry(const RecordEntry&)            = delete;
    RecordEntry& operator=(const RecordEntry&) = delete;
    RecordEntry(RecordEntry&&)                 = delete;
    RecordEntry& operator=(RecordEntry&&)      = delete;

    [[nodiscard]] RecordToken          Token() const noexcept { return IssuedToken; }                  // [-] - Issued at construction
    [[nodiscard]] RecordClassification Classification() const noexcept { return EntryClassification; } // [-] - Outliner category + page

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
  protected:
    // Called by derived constructors to bind Outliner display metadata and Inspector presentation.
    void RegisterEditorDescription(RecordEditorDescription Description);
#endif

  private:
    RecordToken          IssuedToken;           // [-] - From TokenIssuer; authenticated through RecordStore
    RecordClassification EntryClassification;   // [-] - Fixed for the entry's whole lifetime
};

} // namespace Frontier
