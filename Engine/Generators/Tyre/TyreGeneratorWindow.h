//============================================================================================================================================
//                                                         TYREGENERATORWINDOW.H
//============================================================================================================================================
// 📦 The tyre generator as a dockable editor window: the whole application, tab for tab, with the three-dimensional viewport left empty.

#pragma once

#include <cstdint>
#include <string>

#include "Generators/Tyre/RimSpecification.h"
#include "Generators/Tyre/TreadPatternRaster.h"
#include "Generators/Tyre/TreadPatternSpecification.h"
#include "Generators/Tyre/TreadSpecification.h"
#include "Generators/Tyre/TyreAppearanceSpecification.h"

namespace Frontier {

class ControlPanel;

//------------------------------------------------------------------------------------------------------------------------
//                                                        DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One tyre, whole: the carcass it is moulded on, the pattern cut into it, how it is painted and the rim
///    it is fitted to.
/// tag   schema
struct TyreGeneratorDocument
{
    std::string                 Name     = "GZero";
    std::string                 Kind     = "Slick";
    TreadSpecification          Carcass;
    TreadPatternSpecification   Pattern;
    TyreAppearanceSpecification Appearance;
    RimSpecification            Rim;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      WINDOW STATE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Which page of the generator is showing.
/// tag   schema
enum class TyreGeneratorTab : uint8_t
{
    Tyre   = 0,
    Rim    = 1,
    Look   = 2,
    Export = 3
};

/// 📦 Which way the three-dimensional viewport is pointed.
/// tag   schema
enum class TyreGeneratorView : uint8_t
{
    Isometric = 0,
    Side      = 1,
    Tread     = 2,
    Front     = 3
};

/// 📦 Everything the window remembers between frames.
/// note  The depth field and material map are held here rather than rebuilt per frame. A 285/70 R17 tread at
///       384 rows is a little over a megapixel, which is cheap once and ruinous sixty times a second, so the
///       Revision counter is bumped by every edit and the raster is redone only when it has moved.
/// tag   schema
struct TyreGeneratorState
{
    TyreGeneratorDocument Document;

    TyreGeneratorTab      Tab          = TyreGeneratorTab::Tyre;
    TyreGeneratorView     View         = TyreGeneratorView::Isometric;
    TreadMapChannel       TreadChannel = TreadMapChannel::Colour;

    int                   PickedLayer  = -1;   // [-] - index into Document.Pattern.Layers, -1 for none
    int                   PickedDecal  = -1;   // [-] - index into Document.Appearance.Decals, -1 for none
    int                   PickedPreset = 0;    // [-] - index into TyrePresets()

    bool                  Spin         = false;
    bool                  XrayCords    = false;

    uint64_t              Revision     = 1u;   // [-] - bumped by every edit that changes the raster
    uint64_t              RasterRevision = 0u; // [-] - the revision the cached field was built at
    TreadDepthField       Field;
    TreadMaterialMap      Map;
    TreadMapChannel       MapChannel   = TreadMapChannel::Colour;

    std::string           Notice;              // [-] - the transient message strip at the foot of the viewport
    float                 NoticeFade  = 0.0f;  // [-] - 1 fresh, 0 gone

    /// 📦 Marks the document changed, which is the one call every edit path has to make.
    void Touch() noexcept { ++Revision; }
};

/// 📦 Seats a document from one of the shipped presets.
/// in    Index  [-]  into TyrePresets(); out of range seats the first
/// tag   api, nonthrowing
void ApplyTyrePreset(TyreGeneratorState& State, int Index);

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE WINDOW
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Draws the generator. A dockable ImGui window carrying the header tabs, the left control column, the
///    viewport with its floating readouts and the right pattern column.
/// in    Controls  [-]  the shared widget vocabulary, read for its faces
/// in    State     [-]  the document and everything the window remembers
/// in    Open      [-]  cleared when the title bar's close is pressed; nullptr for a window that cannot close
/// note  ⚠️ The three-dimensional viewport is deliberately empty. Every other view in this window is drawn
///       from the specification by arithmetic the host can run with no device at all, which is what lets the
///       whole application be proved headless; a mesh preview cannot be, and a fake one would prove nothing.
/// cost  🚩 One pattern raster per edit, not per frame.
/// tag   api, allocating, nonthrowing
void RecordTyreGeneratorWindow(ControlPanel& Controls, TyreGeneratorState& State, bool* Open);

}   // namespace Frontier
