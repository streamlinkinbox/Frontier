#include "Engine/ContentInterchange/SceneCodec.h"
namespace Frontier {
bool SceneCodec::Encode(const std::string&, const std::vector<TriangleIndex>&,
                        const std::vector<MaterialDescriptor>&, std::string*,
                        const SceneEncodeConfiguration&) noexcept { return true; }
}
