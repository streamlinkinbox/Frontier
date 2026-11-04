/*==========================================================================================================================================
                                                         SURFACESOUPARCHIVE.H
==========================================================================================================================================*/
// 🧩 Reads the demo's corpus surfaces (Exhibits/VirtualizedGeometry/Surfaces/*.soup, written by NestingParityExport.mjs) back
//    into the exact soups the demo generators produced, plus the SHA-256 digests the parity validation compares.
#pragma once
#include <Rendering/VirtualizedGeometry/NestingConstruction.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace Frontier::VirtualizedGeometry
{
// 📝 Decodes one zlib-compressed 'FSU1' soup; std::nullopt with a diagnostic when the file is missing or malformed.
[[nodiscard]] std::optional<SurfaceSoup> ReadSurfaceSoup(const std::filesystem::path& Path, std::string* Diagnostic = nullptr);

// 📝 The exhibit folder: FRONTIER_VIRTUALIZED_GEOMETRY_EXHIBIT when set, otherwise the compiled-in source path.
[[nodiscard]] std::filesystem::path LocateExhibitFolder();

// 📝 Little-endian section writer matching ByteStream in NestingParityExport.mjs.
class SectionStream
{
  public:
    void U8(uint8_t Value) { Bytes.push_back(Value); }
    void U32(uint32_t Value);
    void I32(int32_t Value) { U32(static_cast<uint32_t>(Value)); }
    void F64(double Value);
    void U32List(const std::vector<uint32_t>& Values);
    void F64Triple(const Point3& Values);
    std::vector<uint8_t> Bytes{};
};
[[nodiscard]] std::string DigestSha256(const std::vector<uint8_t>& Bytes);
} // namespace Frontier::VirtualizedGeometry
