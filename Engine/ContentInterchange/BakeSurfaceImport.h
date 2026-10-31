/*============================================================================================================================================
                                                       BAKESURFACEIMPORT.H
============================================================================================================================================*/
// 🧩 Decode OBJ and STL triangle surfaces for the baking workspace.
#pragma once
#include "Engine/ContentInterchange/BakeMesh.h"
#include <filesystem>
#include <string>

namespace Frontier
{
// Low targets require UV coordinates on every OBJ face; STL can only be used as a high source.
[[nodiscard]] BakeMeshSet DecodeBakeSurface(const std::filesystem::path &Path, bool RequireUv);
[[nodiscard]] std::string BakeCorrespondenceName(const std::string &Name);
}
