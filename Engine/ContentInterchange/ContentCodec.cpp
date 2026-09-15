//============================================================================================================================================
//                                                       CONTENTCODEC.CPP
//============================================================================================================================================

#include "ContentCodec.h"
#include "FbxCodec.h"
#include "ObjCodec.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Frontier {

ContentFormatCategory ContentCodec::Classify(const std::string& Path) noexcept
{
    std::string Extension = std::filesystem::path(Path).extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    if (Extension == ".gltf" || Extension == ".glb") return ContentFormatCategory::Gltf;
    if (Extension == ".fbx")                         return ContentFormatCategory::Fbx;
    if (Extension == ".obj")                         return ContentFormatCategory::Obj;
    return ContentFormatCategory::Unknown;
}

const char* ContentCodec::NameOf(ContentFormatCategory Format) noexcept
{
    switch (Format)
    {
        case ContentFormatCategory::Gltf: return "glTF";
        case ContentFormatCategory::Fbx:  return "FBX";
        case ContentFormatCategory::Obj:  return "OBJ";
        default:                          return "unknown";
    }
}

bool ContentCodec::Decode(const std::string& Path, SceneStructure& Out, TextureIndex* Textures, const SceneDecodeConfiguration& Config, std::string* Error) noexcept
{
    switch (Classify(Path))
    {
        case ContentFormatCategory::Gltf: return SceneCodec::Decode(Path, Out, Textures, Config, Error);
        case ContentFormatCategory::Fbx:  return FbxCodec::Decode(Path, Out, Textures, Config, Error);
        case ContentFormatCategory::Obj:  return ObjCodec::Decode(Path, Out, Textures, Config, Error);
        default:
            if (Error) *Error = "unsupported content format '" + std::filesystem::path(Path).extension().string() + "' (expected .gltf, .glb, .fbx or .obj)";
            return false;
    }
}

} // namespace Frontier
