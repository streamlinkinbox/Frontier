/*==========================================================================================================================================
                                                              RECORDENTRY.CPP
==========================================================================================================================================*/
// 🧩 Base of every scene record: constructor registers in RecordStore (+ SceneDirectory/InspectorRuntime under FRONTIER_EDITOR); destructor unregisters.

#include "Engine/Scene/RecordEntry.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneDirectory.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

RecordEntry::RecordEntry(RecordClassification Classification)
    : IssuedToken(TokenIssuer::Issue())
    , EntryClassification(Classification)
{
    RecordStore::Instance().Insert(*this);
#if FRONTIER_EDITOR
    SceneDirectory::Instance().Register(*this);
    InspectorRuntime::Instance().OpenSlot(IssuedToken);
#endif
}

RecordEntry::~RecordEntry()
{
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().Unregister(IssuedToken);
    SceneDirectory::Instance().Unregister(IssuedToken);
#endif
    RecordStore::Instance().Remove(IssuedToken);
}

} // namespace Frontier
