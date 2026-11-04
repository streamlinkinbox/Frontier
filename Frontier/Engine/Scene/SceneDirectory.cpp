/*==========================================================================================================================================
                                                             SCENEDIRECTORY.CPP
==========================================================================================================================================*/
// 🧩 Editor-machinery Outliner model: names, nesting, and inspector/editor metadata for registered records.

#include "Engine/Scene/SceneDirectory.h"

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR

#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Scene/RecordEntry.h"

#include <algorithm>

namespace Frontier
{

namespace
{

uint32_t InterfaceLogToken()
{
    const LogCategory* Category = LogCategories().QueryByName("Interface");
    return Category != nullptr ? Category->Token : 0;
}

} // namespace

SceneDirectory& SceneDirectory::Instance()
{
    static SceneDirectory Directory;
    return Directory;
}

void SceneDirectory::ConfigureActiveSceneHeader(std::string Title, std::string Subtitle)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    if (!Title.empty())    ActiveSceneTitle    = std::move(Title);
    if (!Subtitle.empty()) ActiveSceneSubtitle = std::move(Subtitle);
}

std::string SceneDirectory::QueryActiveSceneTitle() const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return ActiveSceneTitle;
}

std::string SceneDirectory::QueryActiveSceneSubtitle() const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return ActiveSceneSubtitle;
}

void SceneDirectory::Register(const RecordEntry& Entry)
{
    const RecordToken           Token = Entry.Token();
    std::lock_guard<std::mutex> Guard(Mutex);
    if (!Token.IsValid() || Entries.find(Token.Value) != Entries.end())
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                       "SceneDirectory refused registration for token %llu (invalid or duplicate)",
                       static_cast<unsigned long long>(Token.Value));
        return;
    }
    DirectoryEntry& Slot = Entries[Token.Value];
    Slot.Classification  = Entry.Classification();
    RegistrationOrder.push_back(Token);
}

void SceneDirectory::Unregister(RecordToken Token)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    if (Entries.erase(Token.Value) == 0)
    {
        return;
    }
    RegistrationOrder.erase(std::remove(RegistrationOrder.begin(), RegistrationOrder.end(), Token),
                            RegistrationOrder.end());
    for (auto& [KeyValue, Slot] : Entries)
    {
        (void)KeyValue;
        if (Slot.Parent == Token)
        {
            Slot.Parent = RecordToken{};
        }
    }
}

bool SceneDirectory::ConfigureName(RecordToken Token, std::string Name)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end())
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                       "SceneDirectory cannot name unknown token %llu",
                       static_cast<unsigned long long>(Token.Value));
        return false;
    }
    It->second.Name = std::move(Name);
    return true;
}

bool SceneDirectory::ConfigureParent(RecordToken Child, RecordToken Parent)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  ChildIt = Entries.find(Child.Value);
    if (ChildIt == Entries.end() || Child == Parent ||
        (Parent.IsValid() && Entries.find(Parent.Value) == Entries.end()))
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                       "SceneDirectory refused parenting %llu under %llu (unknown or self)",
                       static_cast<unsigned long long>(Child.Value), static_cast<unsigned long long>(Parent.Value));
        return false;
    }
    for (RecordToken Cursor = Parent; Cursor.IsValid();)
    {
        if (Cursor == Child)
        {
            WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                           "SceneDirectory refused parenting %llu under %llu (cycle)",
                           static_cast<unsigned long long>(Child.Value), static_cast<unsigned long long>(Parent.Value));
            return false;
        }
        const auto CursorIt = Entries.find(Cursor.Value);
        Cursor              = CursorIt != Entries.end() ? CursorIt->second.Parent : RecordToken{};
    }
    ChildIt->second.Parent = Parent;
    return true;
}

bool SceneDirectory::ConfigureVisible(RecordToken Token, bool Visible)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end())
    {
        return false;
    }
    It->second.Visible = Visible;
    return true;
}

bool SceneDirectory::ConfigureMetadata(RecordToken Token, SceneDirectoryMetadata Metadata)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end())
    {
        return false;
    }
    It->second.Metadata = std::move(Metadata);
    return true;
}

bool SceneDirectory::QueryVisible(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    return It != Entries.end() && It->second.Visible;
}

bool SceneDirectory::QueryRegistered(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return Entries.find(Token.Value) != Entries.end();
}

std::string SceneDirectory::QueryName(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end())
    {
        return {};
    }
    if (!It->second.Name.empty())
    {
        return It->second.Name;
    }
    return std::string(RecordClassificationName(It->second.Classification)) + " " + std::to_string(Token.Value);
}

RecordToken SceneDirectory::QueryParent(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    return It != Entries.end() ? It->second.Parent : RecordToken{};
}

RecordClassification SceneDirectory::QueryClassification(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    return It != Entries.end() ? It->second.Classification : RecordClassification::Unclassified;
}

SceneDirectoryMetadata SceneDirectory::QueryMetadata(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    return It != Entries.end() ? It->second.Metadata : SceneDirectoryMetadata{};
}

uint32_t SceneDirectory::QueryChildCount(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    uint32_t                    Count = 0;
    for (const auto& [KeyValue, Slot] : Entries)
    {
        (void)KeyValue;
        if (Slot.Parent == Token)
        {
            ++Count;
        }
    }
    return Count;
}

size_t SceneDirectory::QueryCount() const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return Entries.size();
}

void SceneDirectory::CopyRegistrationOrder(std::vector<RecordToken>& Out) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    Out = RegistrationOrder;
}

} // namespace Frontier

#endif // FRONTIER_EDITOR
