/*==========================================================================================================================================
                                                            GEOMETRYINSPECTOR.H
==========================================================================================================================================*/
// 🧩 The geometry inspector: the Project Zero editor's object inspector for the level's scene objects (the Geometry record),
//    with the transform, the quick controls, a Blender-style modifier stack, the material channels and the fracture.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/Inspectors/FractureInspector.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <algorithm>
#include <string>

namespace Frontier
{

class ControlColumn;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class GeometryInspector final : public RecordInspector
{
  public:
    explicit GeometryInspector(GeometryConfiguration& InParameters) noexcept : Parameters(InParameters), FractureView(InParameters.Fracture) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "INSPECTOR / GEOMETRY"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Geometry"; }
    [[nodiscard]] const char* QueryEditorButtonLabel() const noexcept override { return Parameters.Fracture.Enabled ? "Fracture Editor" : nullptr; }
    void                      ConstructEditor(RecordToken Token) override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Visible; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Visible = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] std::string QueryDirectoryReadout() const override;
    [[nodiscard]] const char* QueryDirectoryGlyph() const noexcept override;
    [[nodiscard]] RecordOperationalCondition QueryDirectoryCondition() const noexcept override;
    // 📝 The camera's subject (frame and orbit the selection): the world bounds of the shown copies.
    bool QueryViewTarget(InspectorViewTarget& View) const noexcept override
    {
        const auto& Resolved = Parameters.Resolved;
        if (!Resolved.Bound || Resolved.Shown == 0) return false;
        double Radius = 0.5;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            View.Position[static_cast<size_t>(Axis)] = 0.5 * (Resolved.BoundsMinimum[Axis] + Resolved.BoundsMaximum[Axis]);
            Radius = std::max(Radius, 0.5 * static_cast<double>(Resolved.BoundsMaximum[Axis] - Resolved.BoundsMinimum[Axis]));
        }
        View.Radius = Radius;
        return true;
    }
    void                      ConstructInspector() override;

  private:
    GeometryConfiguration& Parameters;
    FractureInspector      FractureView;            // [-] the Fracture editor's view of the object's fracture
    bool                   NotesOpen    = false;
    int                    MaterialSlot = 0;   // [-] the family shown in the material card
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
