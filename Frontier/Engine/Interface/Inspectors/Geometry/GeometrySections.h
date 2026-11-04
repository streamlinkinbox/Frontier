/*==========================================================================================================================================
                                                            GEOMETRYSECTIONS.H
==========================================================================================================================================*/
// 🧩 The geometry inspector's cards, one class per card: the transform, the quick controls, the modifier stack, the
//    material channels and the fracture, each laid down the page by the GeometryInspector.

#pragma once

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARD LOOKS
//------------------------------------------------------------------------------------------------------------------------

// 📝 .generic-card: #1a1a1a over 1 px #ffffff0d, radius 22 px, padding 18 px 14 px, 12 px apart.
[[nodiscard]] CardLook ResolveGenericCardLook();

// 📝 The Project Zero caption colours: #757575, and the caution amber for a limit the viewport hits.
[[nodiscard]] ImU32 ResolveCautionInk();

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARDS
//------------------------------------------------------------------------------------------------------------------------

class GeometryTransformSection
{
  public:
    // 📝 The object's position, rotation and scale (TransformTable) with resets to where the scene archive placed it.
    static bool Construct(ControlColumn& Page, GeometryConfiguration& Parameters);
};

class GeometryQuickSection
{
  public:
    // 📝 Visible, Dynamic, Cast shadows, GI, Physics and Locked (QuickTiles), as the reference pairs Dynamic and Physics.
    static bool Construct(ControlColumn& Page, GeometryConfiguration& Parameters);
};

class ModifierStackSection
{
  public:
    // 📝 The Blender-style modifier stack: Add modifier, then one card per Array modifier (fold, viewport switch, order, remove,
    //    count, relative and constant offsets, rotation step and pivot) and what the stack makes against what the archive holds.
    static bool Construct(ControlColumn& Page, GeometryConfiguration& Parameters);
};

class GeometryMaterialSection
{
  public:
    // 📝 The material families the object's copies use, one slot shown at a time, with their channel values and controls; the
    //    first edit copies the family into the object's SurfaceSlots (its material block). True when the surfaces changed.
    static bool Construct(ControlColumn& Page, GeometryConfiguration& Parameters, const std::string& RecordName, int& Slot);
};

class GeometryFractureSection
{
  public:
    // 📝 The fracture switch and its Voronoi settings; OpenEditor turns true when the expand action asks for the Fracture editor.
    static bool Construct(ControlColumn& Page, GeometryConfiguration& Parameters, bool& OpenEditor);
};

} // namespace Frontier
