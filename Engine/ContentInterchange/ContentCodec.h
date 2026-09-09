//============================================================================================================================================
//                                                        CONTENTCODEC.H
//============================================================================================================================================
// 🧩 Format dispatcher (ContentInterchange): picks SceneCodec (.gltf/.glb), FbxCodec (.fbx) or ObjCodec (.obj) from the
//    file extension so callers (GameExecution, harnesses, the future Docket) hold one entry point.

#pragma once

#include "SceneCodec.h"

namespace Frontier {

enum class ContentFormatCategory : uint8_t { Unknown = 0, Gltf, Fbx, Obj };

class ContentCodec
{
public:
    [[nodiscard]] static ContentFormatCategory Classify(const std::string& Path) noexcept;
    [[nodiscard]] static const char*           NameOf(ContentFormatCategory Format) noexcept;
    // Decodes by extension. Unknown extensions fail with an explanatory `Error`.
    [[nodiscard]] static bool Decode(const std::string& Path, SceneStructure& Out, TextureIndex* Textures, const SceneDecodeConfiguration& Config, std::string* Error) noexcept;
};

} // namespace Frontier
