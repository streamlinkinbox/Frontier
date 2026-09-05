//============================================================================================================================================
// 📦 ParametricSketcher/Document/SceneDocument.h — Named geometry items with stable identities (curves and surfaces for now; solids in Phase 6)
//============================================================================================================================================
#pragma once

#include "Kernel/SurfaceSpecification.h"
#include <string>
#include <vector>

namespace Frontier
{

enum class ItemKind : uint8_t { Curve, Surface };

struct SceneItem
{
    uint32_t     Identity = 0;                                                          // [-] stable, 1-based, doubles as pick identity
    ItemKind     Kind = ItemKind::Curve;                                                // [-]
    std::string  Name;                                                                  // [-] user-facing, unique
    NurbsCurve   Curve;                                                                 // valid when Kind == Curve
    NurbsSurface Surface;                                                               // valid when Kind == Surface
    bool         Construction = false;                                                  // [-] drawn dashed, never rendered as solid
    bool         Hidden = false;                                                        // [-]
    bool         Selected = false;                                                      // [-]

    [[nodiscard]] Box3 Bounds() const noexcept { return Kind == ItemKind::Curve ? Curve.Bounds() : Surface.Bounds(); }
};

class SceneDocument
{
public:
    [[nodiscard]] SceneItem& AddCurve(std::string Name, NurbsCurve Curve) noexcept;
    [[nodiscard]] SceneItem& AddSurface(std::string Name, NurbsSurface Surface) noexcept;
    bool Remove(uint32_t Identity) noexcept;
    [[nodiscard]] SceneItem*       Find(uint32_t Identity) noexcept;
    [[nodiscard]] SceneItem*       Find(const std::string& Name) noexcept;
    [[nodiscard]] const std::vector<SceneItem>& Items() const noexcept { return Store; }
    [[nodiscard]] std::vector<SceneItem>&       Items() noexcept { return Store; }
    [[nodiscard]] Box3 Bounds(bool SelectedOnly = false) const noexcept;
    [[nodiscard]] std::string UniqueName(const std::string& Stem) const noexcept;
    void Clear() noexcept { Store.clear(); NextIdentity = 1; }

private:
    std::vector<SceneItem> Store;
    uint32_t NextIdentity = 1;
};

} // namespace Frontier
