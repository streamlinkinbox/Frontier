//============================================================================================================================================
//                                                      TYPEFACEREGISTRY.CPP
//============================================================================================================================================
// 🧩 Font family / weight registry — FontCodec discovery → ImGui faces. See TypefaceRegistry.h.

#include "TypefaceRegistry.h"
#include "imgui.h"
#include <algorithm>
#include <cstdlib>

namespace Frontier {

TypefaceRegistry* TypefaceRegistry::Current = nullptr;

namespace {

// Tile preference order — Notch FontsTab.tsx FONTS[] first, then the two extras agreed for the port.
constexpr const char* PreferredFamilies[] =
{
    "Inter", "General Sans", "Archivo", "Space Grotesk", "Clash Display", "Montserrat", "Poppins", "JetBrains Mono",
    "Lato", "Fira Sans"
};

// FontCodec renders "JetBrainsMono" as "Jet Brains Mono" (camel split); normalise both sides for matching.
std::string Squash(std::string_view S)
{
    std::string Out; Out.reserve(S.size());
    for (char C : S) if (C != ' ' && C != '-' && C != '_') Out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
    return Out;
}

constexpr FontWeightCategory Ladder[TypefaceRegistry::WeightCount] =
{
    FontWeightCategory::Thin, FontWeightCategory::ExtraLight, FontWeightCategory::Light, FontWeightCategory::Regular,
    FontWeightCategory::Medium, FontWeightCategory::SemiBold, FontWeightCategory::Bold, FontWeightCategory::ExtraBold,
    FontWeightCategory::Black
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

TypefaceRegistry::TypefaceRegistry() noexcept
    : Families{}, Codec{}, Loaded(false)
{
}

void* TypefaceRegistry::LoadFace(const std::string& Path) noexcept
{
    ImGuiIO& IO = ImGui::GetIO();
    ImFontConfig Config{};
    Config.OversampleH = 2;   // font antialiasing "on" default; the Fonts tab switch flips RasterizerMultiply/oversample
    Config.OversampleV = 1;
    Config.PixelSnapH  = false;
    // Legacy (static-atlas) backends need a size; dynamic-atlas backends ignore it and rasterise per draw size.
    ImFont* Font = IO.Fonts->AddFontFromFileTTF(Path.c_str(), 16.0f, &Config);
    return Font;
}

uint32_t TypefaceRegistry::Load(std::string_view ArchiveRoot) noexcept
{
    if (Loaded) return QueryFamilyCount();
    Loaded = true;
    if (ImGui::GetCurrentContext() == nullptr) return 0u;

    Codec.ScanDirectory(ArchiveRoot);

    for (const char* Wanted : PreferredFamilies)
    {
        const FontFamilyIndex* Index = nullptr;
        for (const FontFamilyIndex& F : Codec.QueryDiscoveredFamilies())
            if (Squash(F.FamilyName) == Squash(Wanted)) { Index = &F; break; }
        if (!Index) continue;

        TypefaceFamily Family;
        Family.Name        = Wanted;                       // canonical spelling (FontCodec's camel-split may differ)
        Family.Description = Index->Description;
        Family.Monospaced  = Squash(Wanted).find("mono") != std::string::npos;

        for (FontWeightCategory W : Ladder)
        {
            const FontVariantIndex* V = nullptr;
            for (const FontVariantIndex& Var : Index->Variants)
                if (Var.Weight == W && Var.Style == FontStyleCategory::Normal) { V = &Var; break; }
            if (!V) continue;
            void* Handle = LoadFace(V->FilePath);
            if (!Handle) continue;
            Family.Faces.push_back(TypefaceFace{ W, Handle });
        }

        // Mandatory trio: a light face, Regular, Bold.
        auto Has = [&](FontWeightCategory W) { return std::any_of(Family.Faces.begin(), Family.Faces.end(), [&](const TypefaceFace& F) { return F.Weight == W; }); };
        const bool LightIsh = Has(FontWeightCategory::Thin) || Has(FontWeightCategory::ExtraLight) || Has(FontWeightCategory::Light);
        if (!LightIsh || !Has(FontWeightCategory::Regular) || !Has(FontWeightCategory::Bold)) continue;

        Families.push_back(std::move(Family));
    }
    return QueryFamilyCount();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        QUERIES
//------------------------------------------------------------------------------------------------------------------------

int32_t TypefaceRegistry::FindFamily(std::string_view Name) const noexcept
{
    const std::string Key = Squash(Name);
    for (size_t I = 0; I < Families.size(); ++I) if (Squash(Families[I].Name) == Key) return static_cast<int32_t>(I);
    return -1;
}

bool TypefaceRegistry::HasWeight(uint32_t FamilyIndex, FontWeightCategory Weight) const noexcept
{
    const TypefaceFamily* F = QueryFamily(FamilyIndex);
    if (!F) return false;
    return std::any_of(F->Faces.begin(), F->Faces.end(), [&](const TypefaceFace& Face) { return Face.Weight == Weight; });
}

const TypefaceFace* TypefaceRegistry::Resolve(uint32_t FamilyIndex, FontWeightCategory Wanted) const noexcept
{
    const TypefaceFamily* F = QueryFamily(FamilyIndex);
    if (!F || F->Faces.empty()) return nullptr;
    const int32_t Want = static_cast<int32_t>(Wanted);
    const TypefaceFace* Best = nullptr; int32_t BestDistance = 1 << 30;
    for (const TypefaceFace& Face : F->Faces)
    {
        const int32_t Have = static_cast<int32_t>(Face.Weight);
        const int32_t Distance = std::abs(Have - Want);
        // CSS-style tie-break: at or above 400 prefer heavier, below 400 prefer lighter.
        const bool PreferThis = Distance < BestDistance
            || (Distance == BestDistance && Best && ((Want >= 400 && Have > static_cast<int32_t>(Best->Weight)) || (Want < 400 && Have < static_cast<int32_t>(Best->Weight))));
        if (PreferThis) { Best = &Face; BestDistance = Distance; }
    }
    return Best;
}

void* TypefaceRegistry::QueryHandle(uint32_t FamilyIndex, FontWeightCategory Wanted) const noexcept
{
    const TypefaceFace* Face = Resolve(FamilyIndex, Wanted);
    return Face ? Face->Handle : nullptr;
}

const char* TypefaceRegistry::WeightLabel(FontWeightCategory Weight) noexcept
{
    switch (Weight)
    {
        case FontWeightCategory::Thin:       return "Thin";
        case FontWeightCategory::ExtraLight: return "ExtraLight";
        case FontWeightCategory::Light:      return "Light";
        case FontWeightCategory::Regular:    return "Regular";
        case FontWeightCategory::Medium:     return "Medium";
        case FontWeightCategory::SemiBold:   return "SemiBold";
        case FontWeightCategory::Bold:       return "Bold";
        case FontWeightCategory::ExtraBold:  return "ExtraBold";
        case FontWeightCategory::Black:      return "Black";
    }
    return "Regular";
}

FontWeightCategory TypefaceRegistry::WeightAt(uint32_t Ordinal) noexcept
{
    return Ladder[std::min(Ordinal, WeightCount - 1u)];
}

uint32_t TypefaceRegistry::WeightOrdinal(FontWeightCategory Weight) noexcept
{
    for (uint32_t I = 0u; I < WeightCount; ++I) if (Ladder[I] == Weight) return I;
    return 3u;
}

} // namespace Frontier
