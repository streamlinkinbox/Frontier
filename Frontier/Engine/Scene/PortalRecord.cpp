/*==========================================================================================================================================
                                                             PORTALRECORD.CPP
==========================================================================================================================================*/
// 🧩 The portal aperture's record and its TOML codec: the aperture extent, the linked peer's name and the spatial placement.

#include "Frontier/Engine/Scene/PortalRecord.h"

#include <toml++/toml.hpp>

#include <algorithm>

namespace Frontier
{

namespace
{

void WriteFloat3(toml::table& OutTable, const char* Key, const float Values[3])
{
    toml::array Array;
    for (int Axis = 0; Axis < 3; ++Axis) Array.push_back(static_cast<double>(Values[Axis]));
    OutTable.insert_or_assign(Key, std::move(Array));
}

void ReadFloat3(const toml::table& InTable, const char* Key, float OutValues[3])
{
    if (const toml::array* Array = InTable[Key].as_array())
        for (int Axis = 0; Axis < 3 && Axis < static_cast<int>(Array->size()); ++Axis)
            OutValues[Axis] = static_cast<float>((*Array)[Axis].value_or(static_cast<double>(OutValues[Axis])));
}

float ReadFloat(const toml::table& InTable, const char* Key, float Fallback)
{
    return static_cast<float>(InTable[Key].value_or(static_cast<double>(Fallback)));
}

} // namespace

PortalRecord::PortalRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Geometry, "Portal", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Box", "Scene objects", "Portal", "#7fd4c1");
}

void PortalRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("aperture_width_m", static_cast<double>(Parameters.ApertureWidthMetres));
    OutTable.insert_or_assign("aperture_height_m", static_cast<double>(Parameters.ApertureHeightMetres));
    OutTable.insert_or_assign("linked_portal", Parameters.LinkedPortalName);
    WriteFloat3(OutTable, "position_m", Parameters.Spatial.PositionMetres);
    WriteFloat3(OutTable, "orientation_deg", Parameters.Spatial.OrientationDegrees);
    WriteFloat3(OutTable, "scale", Parameters.Spatial.Scale);
}

void PortalRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = InTable["enabled"].value_or(Parameters.Enabled);
    Parameters.ApertureWidthMetres  = std::max(0.1f, ReadFloat(InTable, "aperture_width_m", Parameters.ApertureWidthMetres));
    Parameters.ApertureHeightMetres = std::max(0.1f, ReadFloat(InTable, "aperture_height_m", Parameters.ApertureHeightMetres));
    Parameters.LinkedPortalName     = InTable["linked_portal"].value_or(Parameters.LinkedPortalName);
    ReadFloat3(InTable, "position_m", Parameters.Spatial.PositionMetres);
    ReadFloat3(InTable, "orientation_deg", Parameters.Spatial.OrientationDegrees);
    ReadFloat3(InTable, "scale", Parameters.Spatial.Scale);
    Parameters.AdvanceRevision();
}

} // namespace Frontier
