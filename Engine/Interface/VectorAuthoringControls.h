/*============================================================================================================================================
                                                             VECTORAUTHORINGCONTROLS.H
============================================================================================================================================*/
// 🧩 Shared rounded authoring controls, semantic SVG glyphs, tooltips and observable interaction extents for native workspaces.

#pragma once
#include "ControlKit.h"
#include "Engine/ContentInterchange/VectorAuthoringArrayPlacement.h"
#include <functional>
#include <span>
#include <array>
#include <string>
#include <string_view>

namespace Frontier
{
class VectorAuthoringControlSurface
{
public:
    float Scale = 1;
    float ControlHeight = 30;
    std::function<void(std::string_view, PlaneExtent)> RegisterExtent;
    bool Button(
        const char       *Caption,
        float             Width = 0,
        bool              Selected = false,
        std::string_view  Path = {},
        bool              Capsule = false);
    bool Toggle(const char *Caption, bool &Enabled);
    bool Number(
        const char *Caption,
        float      &Quantity,
        float       Minimum,
        float       Maximum);
    bool Range(
        const char *Caption,
        float      &Quantity,
        float       Minimum,
        float       Maximum);
    // 📝 Range's sibling for live-edited fields (modifier parameters): same number box + draggable
    // slider strip updating Quantity every frame while dragged, but Caption's registered extent stays
    // on the NUMBER BOX (typed edits/addressing keep working exactly as with Number) and the strip is
    // registered separately as "<Caption> slider". Range deliberately does the opposite (its drivers
    // address the strip by caption), so the two coexist rather than one changing under the other.
    bool Slider(
        const char *Caption,
        float      &Quantity,
        float       Minimum,
        float       Maximum);
    bool Dropdown(const char *Caption, int &Selection, std::span<const char *const> Captions);
    bool Colour(const char *Caption, std::array<float, 4> &Spectrum);
    bool RenderColourSourcePicker(std::array<float, 4> &Spectrum);
    void Tooltip(const char *Caption, const char *Description);
    void BeginCard(const char *Caption, bool Raised = false, bool Subordinate = false);
    void EndCard();
    bool Array(
        VectorAuthoringArraySpecification &Specification,
        size_t                     LayerCount,
        size_t                     SelectionCount,
        size_t                     Limit,
        bool                       Protected);
    // 📝 Just the layout/profile field widgets shared by the one-shot "Generate array" card above and a
    // per-modifier live-edit card (Blender-style stack entry) - no population caption, no Generate button,
    // no BeginDisabled wrapper (the caller decides that). Suffix keeps on-canvas captions readable while
    // still giving each stacked modifier instance its own unique RegisterExtent/ImGui identity.
    void ArrayFields(VectorAuthoringArraySpecification &Specification, const std::string &Suffix = {});
    // 📝 Same contract as ArrayFields, one sibling per modifier operation - no caption/Generate button, only
    // the raw field widgets a modifier-stack card wraps for its own live-edit/Apply/Delete chrome.
    void MirrorFields(VectorAuthoringMirrorSpecification &Specification, const std::string &Suffix = {});
    void NoiseFields(VectorAuthoringNoiseSpecification &Specification, const std::string &Suffix = {});
    void OffsetFields(VectorAuthoringOffsetSpecification &Specification, const std::string &Suffix = {});
    void ColourFields(VectorAuthoringColourSpecification &Specification, const std::string &Suffix = {});
private:
    int LastCardFrame = -1;
    int CardOrdinal = 0;
    void Register(const char *Caption);
    bool RangeCore(const char *Caption, float &Quantity, float Minimum, float Maximum, bool StripOwnsCaption);
};
}
