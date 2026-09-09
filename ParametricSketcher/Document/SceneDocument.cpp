//============================================================================================================================================
// 📦 ParametricSketcher/Document/SceneDocument.cpp — Item store
//============================================================================================================================================

#include "SceneDocument.h"
#include <algorithm>

namespace Frontier
{

SceneItem& SceneDocument::AddCurve(std::string Name, NurbsCurve Curve) noexcept
{
    SceneItem Item;
    Item.Identity = NextIdentity++;
    Item.Kind = ItemKind::Curve;
    Item.Name = UniqueName(Name.empty() ? "Curve" : Name);
    Item.Curve = std::move(Curve);
    Store.push_back(std::move(Item));
    return Store.back();
}

SceneItem& SceneDocument::AddSurface(std::string Name, NurbsSurface Surface) noexcept
{
    SceneItem Item;
    Item.Identity = NextIdentity++;
    Item.Kind = ItemKind::Surface;
    Item.Name = UniqueName(Name.empty() ? "Surface" : Name);
    Item.Surface = std::move(Surface);
    Store.push_back(std::move(Item));
    return Store.back();
}

bool SceneDocument::Remove(uint32_t Identity) noexcept
{
    auto It = std::find_if(Store.begin(), Store.end(), [&](const SceneItem& I) { return I.Identity == Identity; });
    if (It == Store.end()) return false;
    Store.erase(It);
    return true;
}

SceneItem* SceneDocument::Find(uint32_t Identity) noexcept
{
    for (SceneItem& I : Store) if (I.Identity == Identity) return &I;
    return nullptr;
}

SceneItem* SceneDocument::Find(const std::string& Name) noexcept
{
    for (SceneItem& I : Store) if (I.Name == Name) return &I;
    return nullptr;
}

Box3 SceneDocument::Bounds(bool SelectedOnly) const noexcept
{
    Box3 B;
    for (const SceneItem& I : Store)
        if (!I.Hidden && (!SelectedOnly || I.Selected)) B.Include(I.Bounds());
    return B;
}

std::string SceneDocument::UniqueName(const std::string& Stem) const noexcept
{
    auto Taken = [&](const std::string& N) { return std::any_of(Store.begin(), Store.end(), [&](const SceneItem& I) { return I.Name == N; }); };
    if (!Taken(Stem)) return Stem;
    for (int K = 2;; ++K)
    {
        std::string Candidate = Stem + "." + std::to_string(K);
        if (!Taken(Candidate)) return Candidate;
    }
}

} // namespace Frontier
