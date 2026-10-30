// 🧩 Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.

#include "Engine/Scene/RecordEntry.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneDirectory.h"

#include <utility>
#include <stdexcept>

namespace Frontier
{

RecordEntry::RecordEntry(RecordClassification Classification)
    : IssuedToken(TokenIssuer::Issue())
    , EntryClassification(Classification)
{
    if (!RecordStore::Instance().Insert(*this))
        throw std::runtime_error("Scene record token registration refused.");
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    try
    {
        SceneDirectory::Instance().Register(*this);
    }
    catch (...)
    {
        // The complete constructor never ran, so its destructor cannot roll back registration.
        SceneDirectory::Instance().Unregister(IssuedToken);
        RecordStore::Instance().Remove(IssuedToken);
        throw;
    }
#endif
}

#if defined(FRONTIER_EDITOR_PROFILE)
void RecordEntry::RegisterEditorDescription(RecordEditorDescription Description)
{
    SceneDirectory::Instance().ConfigureEditorDescription(IssuedToken, std::move(Description));
}
#endif

RecordEntry::~RecordEntry()
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    SceneDirectory::Instance().Unregister(IssuedToken);
#endif
    RecordStore::Instance().Remove(IssuedToken);
}

} // namespace Frontier
