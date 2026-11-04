/*==========================================================================================================================================
                                                        INSPECTORRUNTIME.CPP
==========================================================================================================================================*/
// 🧩 Central Editor runtime managing modular RecordInspector attachments, lifecycle slots, and *Editor dispatch.

#include "Engine/Interface/InspectorRuntime.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/InspectorControls.h"
#include <imgui.h>
#endif

#include <algorithm>
#include <cstdio>
#include <string>

namespace Frontier
{

InspectorRuntime& InspectorRuntime::Instance() noexcept
{
    static InspectorRuntime Runtime;
    return Runtime;
}

void InspectorRuntime::OpenSlot([[maybe_unused]] RecordToken Token)
{
#if FRONTIER_EDITOR
    if (Token.IsValid())
    {
        (void)Slots[Token.Value];
    }
#endif
}

void InspectorRuntime::Unregister([[maybe_unused]] RecordToken Token) noexcept
{
#if FRONTIER_EDITOR
    if (ActiveEditorToken == Token)
    {
        ActiveEditorToken = RecordToken{};
    }
    Slots.erase(Token.Value);
#endif
}

bool InspectorRuntime::BindInspector([[maybe_unused]] RecordToken                      Token,
                                     [[maybe_unused]] std::unique_ptr<RecordInspector> Inspector)
{
#if FRONTIER_EDITOR
    if (!Token.IsValid() || !Inspector)
    {
        return false;
    }
    auto It = Slots.find(Token.Value);
    if (It == Slots.end())
    {
        return false;
    }
    It->second.Inspectors.clear();
    It->second.Inspectors.push_back(std::move(Inspector));
    return true;
#else
    return false;
#endif
}

bool InspectorRuntime::AttachInspector([[maybe_unused]] RecordToken                      Token,
                                       [[maybe_unused]] std::unique_ptr<RecordInspector> Inspector)
{
#if FRONTIER_EDITOR
    if (!Token.IsValid() || !Inspector)
    {
        return false;
    }
    auto It = Slots.find(Token.Value);
    if (It == Slots.end())
    {
        return false;
    }
    It->second.Inspectors.push_back(std::move(Inspector));
    return true;
#else
    return false;
#endif
}

RecordInspector* InspectorRuntime::FindInspector([[maybe_unused]] RecordToken Token) noexcept
{
#if FRONTIER_EDITOR
    const auto It = Slots.find(Token.Value);
    if (It == Slots.end() || It->second.Inspectors.empty())
    {
        return nullptr;
    }
    return It->second.Inspectors.front().get();
#else
    return nullptr;
#endif
}

const RecordInspector* InspectorRuntime::FindInspector([[maybe_unused]] RecordToken Token) const noexcept
{
#if FRONTIER_EDITOR
    const auto It = Slots.find(Token.Value);
    if (It == Slots.end() || It->second.Inspectors.empty())
    {
        return nullptr;
    }
    return It->second.Inspectors.front().get();
#else
    return nullptr;
#endif
}

size_t InspectorRuntime::QueryInspectorCount([[maybe_unused]] RecordToken Token) const noexcept
{
#if FRONTIER_EDITOR
    const auto It = Slots.find(Token.Value);
    return It != Slots.end() ? It->second.Inspectors.size() : 0u;
#else
    return 0u;
#endif
}

bool InspectorRuntime::QueryHasEditor([[maybe_unused]] RecordToken Token) const noexcept
{
#if FRONTIER_EDITOR
    const auto It = Slots.find(Token.Value);
    if (It == Slots.end())
    {
        return false;
    }
    for (const auto& Entry : It->second.Inspectors)
    {
        if (Entry && Entry->QueryEditorButtonLabel() != nullptr && Entry->QueryEditorButtonLabel()[0] != '\0')
        {
            return true;
        }
    }
    return false;
#else
    return false;
#endif
}

bool InspectorRuntime::QuerySlotOpen([[maybe_unused]] RecordToken Token) const noexcept
{
#if FRONTIER_EDITOR
    return Slots.find(Token.Value) != Slots.end();
#else
    return false;
#endif
}

uint64_t InspectorRuntime::QuerySlotCount() const noexcept
{
#if FRONTIER_EDITOR
    return static_cast<uint64_t>(Slots.size());
#else
    return 0;
#endif
}

uint64_t InspectorRuntime::QueryBoundCount() const noexcept
{
#if FRONTIER_EDITOR
    uint64_t Count = 0;
    for (const auto& [Key, Entry] : Slots)
    {
        (void)Key;
        if (!Entry.Inspectors.empty())
        {
            ++Count;
        }
    }
    return Count;
#else
    return 0;
#endif
}

bool InspectorRuntime::RequestEditorOpen([[maybe_unused]] RecordToken Token) noexcept
{
#if FRONTIER_EDITOR
    if (!Token.IsValid() || !QueryHasEditor(Token))
    {
        return false;
    }
    ActiveEditorToken = Token;
    return true;
#else
    return false;
#endif
}

void InspectorRuntime::CloseActiveEditor() noexcept
{
#if FRONTIER_EDITOR
    ActiveEditorToken = RecordToken{};
#endif
}

RecordToken InspectorRuntime::QueryActiveEditorToken() const noexcept
{
#if FRONTIER_EDITOR
    return ActiveEditorToken;
#else
    return RecordToken{};
#endif
}

void InspectorRuntime::ConstructActiveEditor()
{
#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    if (!ActiveEditorToken.IsValid())
    {
        return;
    }
    if (!SceneDirectory::Instance().QueryRegistered(ActiveEditorToken))
    {
        CloseActiveEditor();
        return;
    }
    auto It = Slots.find(ActiveEditorToken.Value);
    if (It == Slots.end())
    {
        CloseActiveEditor();
        return;
    }
    for (auto& Entry : It->second.Inspectors)
    {
        if (Entry && Entry->QueryEditorButtonLabel() != nullptr && Entry->QueryEditorButtonLabel()[0] != '\0')
        {
            Entry->ConstructEditor(ActiveEditorToken);
            return;
        }
    }
    CloseActiveEditor();
#endif
}

void InspectorRuntime::RequestSelection(std::string_view TypeIdentifier)
{
    PendingSelectionType.assign(TypeIdentifier.data(), TypeIdentifier.size());
}

std::string InspectorRuntime::ConsumeSelectionRequest()
{
    std::string Request;
    Request.swap(PendingSelectionType);
    return Request;
}

void InspectorRuntime::RequestWorkspaceAction(std::string Action)
{
    if (!Action.empty() && PendingWorkspaceActions.size() < 64) PendingWorkspaceActions.push_back(std::move(Action));
}

std::vector<std::string> InspectorRuntime::ConsumeWorkspaceActions()
{
    std::vector<std::string> Actions;
    Actions.swap(PendingWorkspaceActions);
    return Actions;
}

void InspectorRuntime::ConfigureInterfaceScale(float Scale) noexcept
{
    InterfaceScale = Scale > 0.0f ? Scale : 1.0f;
}

float InspectorRuntime::ResolveInterfaceScale() const noexcept
{
    return InterfaceScale;
}

void InspectorRuntime::RegisterSolarPlacementSolver(SolarPlacementSolver Solver) noexcept
{
    SolarSolver = Solver;
}

SolarPlacementSolver InspectorRuntime::QuerySolarPlacementSolver() const noexcept
{
    return SolarSolver;
}

void InspectorRuntime::InspectRecord([[maybe_unused]] RecordToken Token, bool& OutDeleteRequested)
{
    OutDeleteRequested = false;

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    SceneDirectory&         Directory = SceneDirectory::Instance();
    const InspectorPalette  Palette   = InspectorPalette::Resolve();
    const EditorTypefaces   Faces     = EditorTypefaces::Resolve();

    if (!Token.IsValid() || !Directory.QueryRegistered(Token))
    {
        InspectorControls::ConstructCrumbBar(Palette, Faces, nullptr);
        ImGui::Dummy(ImVec2(1.0f, 32.0f));
        ImDrawList*  DrawList = ImGui::GetWindowDrawList();
        const ImVec2 Pos      = ImGui::GetCursorScreenPos();
        DrawList->AddText(Faces.Medium, 13.0f, ImVec2(Pos.x + 20.0f, Pos.y),
                          Palette.TextMuted, "Select a record in Outliner to inspect its configurations.");
        return;
    }

    const SceneDirectoryMetadata Meta = Directory.QueryMetadata(Token);
    const std::string            Name = Directory.QueryName(Token);
    InspectedToken                    = Token;

    // 📝 A page that occupies the column (the demo's environment Inspector pages) lays it out itself, edge to edge.
    if (auto Occupied = Slots.find(Token.Value);
        Occupied != Slots.end() && Occupied->second.Inspectors.size() == 1 && Occupied->second.Inspectors.front()->QueryOccupiesColumn())
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginChild("##InspectorPageRegion", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar))
        {
            Occupied->second.Inspectors.front()->ConstructInspector();
            if (Meta.RemovableInEditor)
            {
                // 📝 The page's 20 px inset on both sides: the button sizes itself to the region it is given.
                const float Inset = 20.0f * InterfaceScale;
                ImGui::SetCursorPosX(Inset);
                if (ImGui::BeginChild("##RemoveSceneRecordRegion", ImVec2(std::max(140.0f, ImGui::GetContentRegionAvail().x - Inset), 30.0f),
                                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
                {
                    if (InspectorControls::ConstructDangerButton(Palette, Faces, "##RemoveSceneRecordBtn", "Remove from Scene"))
                    {
                        OutDeleteRequested = true;
                    }
                }
                ImGui::EndChild();
                ImGui::Dummy(ImVec2(1.0f, Inset));
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }

    InspectorControls::ConstructCrumbBar(
        Palette, Faces, Meta.GroupName.empty() ? RecordClassificationName(Directory.QueryClassification(Token)) : Meta.GroupName.c_str());

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    // 📝 No scrollbars (owner's request): the wheel scrolls the page, eased, with Patch H's overscroll spring.
    if (ImGui::BeginChild("##InspectorRuntimeScrollRegion", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar))
    {
        auto SlotIt = Slots.find(Token.Value);
        if (SlotIt == Slots.end() || SlotIt->second.Inspectors.empty())
        {
            bool DummyToggle = true;
            (void)InspectorControls::ConstructRecordHeader(
                Palette, Faces, RecordClassificationName(Directory.QueryClassification(Token)),
                Name.c_str(), nullptr, false, DummyToggle);
        }
        else
        {
            auto&       List    = SlotIt->second.Inspectors;
            const auto& Primary = List.front();

            const char* EditorLabel = nullptr;
            for (const auto& Entry : List)
            {
                if (Entry && Entry->QueryEditorButtonLabel() != nullptr && Entry->QueryEditorButtonLabel()[0] != '\0')
                {
                    EditorLabel = Entry->QueryEditorButtonLabel();
                    break;
                }
            }

            bool EnabledState = Primary->QueryToggleEnabled();
            const bool PrevEnabled = EnabledState;
            if (InspectorControls::ConstructRecordHeader(
                    Palette, Faces,
                    List.size() > 1 ? "COMPOSED RECORD CONFIGURATIONS" : Primary->QueryEyebrowText(),
                    Name.c_str(), EditorLabel, Primary->QueryToggleSupported(), EnabledState))
            {
                (void)RequestEditorOpen(Token);
            }
            if (Primary->QueryToggleSupported() && EnabledState != PrevEnabled)
            {
                Primary->ConfigureToggle(EnabledState);
            }

            std::vector<InspectorSwitchEntry> Switches;
            for (size_t Index = 0; Index < List.size(); ++Index)
            {
                RecordInspector& Current = *List[Index];
                ImGui::PushID(static_cast<int>(Index));

                if (List.size() > 1)
                {
                    InspectorControls::ConstructSectionDivider(
                        Palette, Faces, Current.QueryEyebrowText(), Current.QuerySectionNote());
                }

                Switches.clear();
                Current.EnumerateSwitches(Switches);
                if (!Switches.empty())
                {
                    if (List.size() == 1)
                    {
                        InspectorControls::ConstructSectionDivider(Palette, Faces, "PROPERTIES", Current.QuerySectionNote());
                    }
                    char PrefixBuf[32] = {};
                    std::snprintf(PrefixBuf, sizeof(PrefixBuf), "##InspSw_%zu", Index);
                    (void)InspectorControls::ConstructSwitchGrid(Palette, Faces, PrefixBuf, Switches.data(), Switches.size());
                }

                Current.ConstructInspector();
                ImGui::Dummy(ImVec2(1.0f, 8.0f));
                ImGui::PopID();
            }
        }

        if (Meta.RemovableInEditor)
        {
            ImGui::Dummy(ImVec2(1.0f, 6.0f));
            if (InspectorControls::ConstructDangerButton(Palette, Faces, "##RemoveSceneRecordBtn", "Remove from Scene"))
            {
                OutDeleteRequested = true;
            }
        }

        ImGui::Dummy(ImVec2(1.0f, 10.0f));
        ImDrawList*  DrawList = ImGui::GetWindowDrawList();
        const ImVec2 FootPos  = ImGui::GetCursorScreenPos();
        const float  AvailW   = std::max(80.0f, ImGui::GetContentRegionAvail().x);
        DrawList->AddLine(FootPos, ImVec2(FootPos.x + AvailW, FootPos.y), Palette.EdgeBorder, 1.0f);
        DrawList->AddText(Faces.Regular, 10.0f, ImVec2(FootPos.x, FootPos.y + 8.0f), Palette.TextFaint, Name.c_str());
        ImGui::Dummy(ImVec2(AvailW, 24.0f));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
#endif
}

} // namespace Frontier
