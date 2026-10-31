// 🧩 Editor-machinery Outliner model: names, folder enclosures, and inspector descriptions, Editor/Development only.

#include "Engine/Scene/SceneDirectory.h"

#include "Engine/Diagnostics/BuildConfiguration.h"

// 📝 The directory is editor machinery: in Shipping this translation unit is intentionally empty (and Shipping
//    builds do not link it at all), so a Shipping binary contains no directory (plan §5/§6).
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)

#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Scene/RecordEntry.h"

#include <algorithm>
#include <utility>

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
        return;   // Never registered (or already gone): destruction stays silent and safe.
    }
    RegistrationOrder.erase(std::remove(RegistrationOrder.begin(), RegistrationOrder.end(), Token),
                            RegistrationOrder.end());
    for (auto& [KeyValue, Slot] : Entries)
    {
        (void)KeyValue;
        if (Slot.Enclosure == Token)
        {
            Slot.Enclosure = RecordToken{};   // Re-rooted: no dangling up-links survive destruction.
        }
    }
}

bool SceneDirectory::ConfigureName(RecordToken Token, std::string Name)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    if (Iterator == Entries.end())
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                       "SceneDirectory cannot name unknown token %llu",
                       static_cast<unsigned long long>(Token.Value));
        return false;
    }
    Iterator->second.Name = std::move(Name);
    if (Iterator->second.Description.has_value())
    {
        Iterator->second.Description->DisplayName = Iterator->second.Name;
    }
    return true;
}

bool SceneDirectory::ConfigureParent(RecordToken Target, RecordToken Enclosure)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  TargetIterator = Entries.find(Target.Value);
    if (TargetIterator == Entries.end() || Target == Enclosure ||
        (Enclosure.IsValid() && Entries.find(Enclosure.Value) == Entries.end()))
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                       "SceneDirectory refused parenting %llu under %llu (unknown or self)",
                       static_cast<unsigned long long>(Target.Value), static_cast<unsigned long long>(Enclosure.Value));
        return false;
    }
    // Walk the proposed enclosure's up-link chain: meeting Target would close a cycle.
    for (RecordToken Cursor = Enclosure; Cursor.IsValid();)
    {
        if (Cursor == Target)
        {
            WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                           "SceneDirectory refused parenting %llu under %llu (cycle)",
                           static_cast<unsigned long long>(Target.Value), static_cast<unsigned long long>(Enclosure.Value));
            return false;
        }
        const auto CursorIterator = Entries.find(Cursor.Value);
        Cursor                    = CursorIterator != Entries.end() ? CursorIterator->second.Enclosure : RecordToken{};
    }
    TargetIterator->second.Enclosure = Enclosure;
    return true;
}

bool SceneDirectory::ConfigureVisible(RecordToken Token, bool Visible)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    if (Iterator == Entries.end())
    {
        return false;
    }
    Iterator->second.Visible = Visible;
    if (Iterator->second.Description.has_value() && Iterator->second.Description->EnabledTarget != nullptr)
    {
        *Iterator->second.Description->EnabledTarget = Visible;
    }
    return true;
}

bool SceneDirectory::ConfigureCascadeVisible(RecordToken Token, bool Visible)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  RootIterator = Entries.find(Token.Value);
    if (RootIterator == Entries.end())
    {
        return false;
    }

    std::vector<uint64_t> Queue{Token.Value};
    for (size_t Index = 0; Index < Queue.size(); ++Index)
    {
        const uint64_t Current = Queue[Index];
        const auto     CurrentIterator = Entries.find(Current);
        if (CurrentIterator == Entries.end())
        {
            continue;
        }
        CurrentIterator->second.Visible = Visible;
        if (CurrentIterator->second.Description.has_value() &&
            CurrentIterator->second.Description->EnabledTarget != nullptr)
        {
            *CurrentIterator->second.Description->EnabledTarget = Visible;
        }
        for (const auto& [CandidateKey, CandidateSlot] : Entries)
        {
            if (CandidateSlot.Enclosure.Value == Current)
            {
                Queue.push_back(CandidateKey);
            }
        }
    }
    return true;
}

bool SceneDirectory::QueryVisible(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    return Iterator != Entries.end() && Iterator->second.Visible;
}

bool SceneDirectory::ConfigureAccentColour(RecordToken Token, uint32_t PackedRgba)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    if (Iterator == Entries.end())
    {
        return false;
    }
    Iterator->second.AccentColour = PackedRgba;
    if (Iterator->second.Description.has_value())
    {
        Iterator->second.Description->AccentColour = PackedRgba;
    }
    return true;
}

uint32_t SceneDirectory::QueryAccentColour(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    return Iterator != Entries.end() ? Iterator->second.AccentColour : 0u;
}

bool SceneDirectory::ConfigureEditorDescription(RecordToken Token, RecordEditorDescription Description)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    if (Iterator == Entries.end())
    {
        return false;
    }
    Iterator->second.Description = std::move(Description);
    return true;
}

const RecordEditorDescription* SceneDirectory::QueryEditorDescription(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    if (Iterator == Entries.end() || !Iterator->second.Description.has_value())
    {
        return nullptr;
    }
    return &(*Iterator->second.Description);
}

std::string SceneDirectory::QueryLiveBadge(RecordToken Token) const
{
    std::function<std::string()> Evaluator;
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        const auto                  Iterator = Entries.find(Token.Value);
        if (Iterator != Entries.end() && Iterator->second.Description.has_value() &&
            Iterator->second.Description->QueryLiveBadge)
        {
            Evaluator = Iterator->second.Description->QueryLiveBadge;
        }
    }
    return Evaluator ? Evaluator() : std::string{};
}

RecordToken SceneDirectory::FindTokenByIdentifier(std::string_view Identifier) const
{
    if (Identifier.empty())
    {
        return RecordToken{};
    }
    std::lock_guard<std::mutex> Guard(Mutex);
    for (const RecordToken Token : RegistrationOrder)
    {
        const auto Iterator = Entries.find(Token.Value);
        if (Iterator != Entries.end() && Iterator->second.Description.has_value() &&
            Iterator->second.Description->Identifier == Identifier)
        {
            return Token;
        }
    }
    return RecordToken{};
}

void SceneDirectory::SynchronizeEnabledFromDescriptions()
{
    std::lock_guard<std::mutex> Guard(Mutex);
    for (auto& [KeyValue, Slot] : Entries)
    {
        (void)KeyValue;
        if (Slot.Description.has_value() && Slot.Description->EnabledTarget != nullptr)
        {
            Slot.Visible = *Slot.Description->EnabledTarget;
        }
    }
}

bool SceneDirectory::QueryRegistered(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return Entries.find(Token.Value) != Entries.end();
}

std::string SceneDirectory::QueryName(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    if (Iterator == Entries.end())
    {
        return {};
    }
    if (!Iterator->second.Name.empty())
    {
        return Iterator->second.Name;
    }
    return std::string(RecordClassificationName(Iterator->second.Classification)) + " " + std::to_string(Token.Value);
}

RecordToken SceneDirectory::QueryParent(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    return Iterator != Entries.end() ? Iterator->second.Enclosure : RecordToken{};
}

RecordClassification SceneDirectory::QueryClassification(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  Iterator = Entries.find(Token.Value);
    return Iterator != Entries.end() ? Iterator->second.Classification : RecordClassification::Unclassified;
}

uint32_t SceneDirectory::QueryChildCount(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    uint32_t                    Count = 0;
    for (const auto& [KeyValue, Slot] : Entries)
    {
        (void)KeyValue;
        if (Slot.Enclosure == Token)
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

#endif // Editor/Development profiles
