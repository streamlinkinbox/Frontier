//=============================================================================================================================================
// SolidArcEditorHost.h
//=============================================================================================================================================
// ImGui editor shell for SolidArc. It seats the same Frontier outliner and viewport panels used by Project-Zero,
// with SolidArc-specific row mapping supplied by SolidArcOutlinerAdapter.

#pragma once

#include "SolidArcOutlinerAdapter.h"
#include "../../../../../Engine/Editor/ControlPanel.h"
#include "../../../../../Engine/Editor/InspectorPanel.h"
#include "../../../../../Engine/Editor/OutlinerPanel.h"
#include "../../../../../Engine/Editor/ViewportPanel.h"

#include <cstdint>

namespace Frontier {

class SolidArcEditorHost
{
public:
    SolidArcEditorHost() noexcept;

    void ApplyTheme() noexcept;
    void Record(ConsoleHost& Host) noexcept;

    [[nodiscard]] uint32_t QueryPickedFigureIdentity() const noexcept;
    [[nodiscard]] float    QueryViewWidth() const noexcept { return Viewport_.QueryViewWidth(); }
    [[nodiscard]] float    QueryViewHeight() const noexcept { return Viewport_.QueryViewHeight(); }

private:
    void ConstructLayout() noexcept;

    ControlPanel Controls_;
    OutlinerPanel Outliner_;
    ViewportPanel Viewport_;
    InspectorPanel Inspector_;

    bool OutlinerTabOpen_ = true;
    bool ViewportTabOpen_ = true;
    bool InspectorTabOpen_ = true;
    ImGuiID LeftColumn_   = 0u;
    ImGuiID CentreColumn_ = 0u;
    ImGuiID RightColumn_  = 0u;
    bool LayoutSeated_    = false;

    std::vector<EditorInstance> Rows_{kMaxEditorInstances};
    std::vector<SolidArcOutlinerBinding> Bindings_{kMaxEditorInstances};
    uint32_t RowCount_ = 0u;
    EditorReadout Readout_ = {};
    EditorSheet PickedSheet_ = {};
    RasterImage ViewImage_ = {};
};

} // namespace Frontier
