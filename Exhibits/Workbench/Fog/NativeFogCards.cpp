//==============================================================================================================================================
//                                                            NATIVEFOGCARDS.CPP
//==============================================================================================================================================
// 📦 Executed proof: records the three native fog cards — beam chamber, density profile and volume shape — from real ImGui draw commands.

#include "FogPanelSurface.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Frontier;
using namespace Frontier::FogCards;

namespace
{

unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition)
    {
        throw std::runtime_error(Claim);
    }
}

// The surrounding card is the one the native fog inspector already draws; only its contents are new.
void CardChrome(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall, const char* Title)
{
    const int First = Draw->VtxBuffer.Size;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, IM_COL32_WHITE, 22.0f);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(Draw, First, Draw->VtxBuffer.Size, Spot,
                                                  { Spot.x + Wide, Spot.y + Tall }, IM_COL32(37, 37, 37, 255),
                                                  IM_COL32(32, 32, 32, 255));
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, IM_COL32(52, 52, 52, 255), 22.0f);
    Draw->AddText(Face, 12.0f, { Spot.x + 24.0f, Spot.y + 16.0f }, IM_COL32(233, 233, 233, 255), Title);
}

}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/FogNative");
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig FaceConfig;
    std::snprintf(FaceConfig.Name, sizeof(FaceConfig.Name), "Sun reference / regular");
    ImFont* Face = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14,
                                                &FaceConfig);
    ImGui::StyleColorsDark();

    int   Width = 420, Height = 1400;
    float Measured = 0;

    //  What the frame paints this pass; the lambdas below swap it between captures -----------------------
    enum class Subject { Chamber, Profile, Volume };
    Subject   Shown  = Subject::Chamber;
    FogValues Medium;
    FogShape  Shape;
    FogShape  Cycled;
    float     Centre[3] = { 0.0f, 0.0f, 0.0f };
    float     DensitySlot = 0.0f, FalloffSlot = 0.0f;
    bool      Editable = false;

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginChild("##fog", { 0, float(Height) }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Face, 14.0f);

        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 Origin{ 10.0f, 10.0f };
        const float  Card = float(Width) - 20.0f;
        const float  Body = Card - 48.0f;
        float Tall = 0.0f;
        if (Shown == Subject::Chamber)
        {
            Tall = 56.0f + 18.0f + ChamberHeight() + 24.0f;
            CardChrome(Draw, Face, Origin, Card, Tall, "Medium");
            PaintChamber(Draw, Face, { Origin.x + 24.0f, Origin.y + 74.0f }, Body, Medium);
        }
        else if (Shown == Subject::Profile)
        {
            const float Inner = ProfileHeight(Face, Body);
            Tall = 56.0f + Inner + 24.0f;
            CardChrome(Draw, Face, Origin, Card, Tall, "Height and tint");
            PaintProfile(Draw, Face, { Origin.x + 24.0f, Origin.y + 56.0f }, Body, Medium,
                         Editable ? &DensitySlot : nullptr, Editable ? &FalloffSlot : nullptr);
        }
        else
        {
            const float Inner = VolumeHeight(Face, Body, Shape);
            Tall = 56.0f + Inner + 24.0f;
            CardChrome(Draw, Face, Origin, Card, Tall, "Fog volume shape");
            PaintVolume(Draw, Face, { Origin.x + 24.0f, Origin.y + 56.0f }, Body, Shape, Centre,
                        Editable ? &Cycled : nullptr);
        }

        ImGui::PopFont();
        ImGui::SetCursorScreenPos({ 0, 0 });
        ImGui::Dummy({ float(Width), Tall + 20.0f });
        Measured = ImGui::GetCursorPosY();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        Height = 2600;
        Tick();
        Height = static_cast<int>(Measured) + 2;
        for (int Frame = 0; Frame < 3; ++Frame)
        {
            Tick();
        }
        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 5);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
        {
            FrontierProof::Draw(List, Pixels.data(), Width, Height, { 0, 0 }, { 1, 1 }, ImTextureID(), {});
        }
        const std::string Path = "Exhibits/Gallery/FogNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "capture written");
    };

    //-----------------------------------------------------------------------------------------------------
    // Beam chamber — one shared chamber, three media.
    //-----------------------------------------------------------------------------------------------------
    Shown = Subject::Chamber;

    Medium = FogValues{};
    Medium.Kind = FogKind::Height;
    Medium.Enabled = true;
    Medium.Tint[0] = Medium.Tint[1] = Medium.Tint[2] = 1.0f;
    Capture("ChamberHeight");
    {
        const float Thick = Extinction(Medium, 25.0f);
        Check(std::fabs(Thick - 0.0187882f) < 1e-5f, "height fog extinction is density folded to the 25 m probe");
        Check(std::fabs(-std::log(0.02f) / Thick - 208.24f) < 0.5f, "height fog clears 2 % at 208 m");
        Check(std::lround(std::exp(-Thick * 120.0f) * 100.0f) == 10, "10 % physical transmission at 120 m");
    }

    Medium = FogValues{};
    Medium.Kind = FogKind::Aerial;
    Medium.Enabled = true;
    Medium.Density = 1.0f;
    Capture("ChamberAerial");
    {
        const float Thick = Extinction(Medium, 2.0f);
        Check(std::fabs(Thick - 0.001f) < 1e-7f, "aerial density scales by a thousandth");
        Check(std::fabs(-std::log(0.02f) / Thick / 1000.0f - 3.912f) < 0.01f, "aerial fog clears 2 % at 3.9 km");
        Check(std::lround(std::exp(-Thick * 400.0f) * 100.0f) == 67, "67 % physical transmission at 400 m");
    }

    Medium = FogValues{};
    Medium.Kind = FogKind::Local;
    Medium.Enabled = false;
    Medium.Density = 1.0f;
    Medium.Coverage = 0.55f;
    Capture("ChamberLocal");
    {
        const float Thick = Extinction(Medium, 2.0f);
        Check(std::fabs(Thick - 0.0055f) < 1e-6f, "local extinction is density times coverage per hundred");
        Check(std::fabs(-std::log(0.02f) / Thick - 711.3f) < 0.5f, "local fog clears 2 % at 711 m");
        Check(std::lround(std::exp(-Thick * 400.0f) * 100.0f) == 11, "11 % physical transmission at 400 m");
        Check(std::strcmp(SpreadName(FogKind::Local), "Anisotropy") == 0, "local fog spreads by anisotropy");
    }

    //-----------------------------------------------------------------------------------------------------
    // Density profile — the authored Height Fog curve, dragged in both axes at once.
    //-----------------------------------------------------------------------------------------------------
    Shown = Subject::Profile;
    Medium = FogValues{};
    Medium.Kind = FogKind::Height;
    Medium.Enabled = true;
    Capture("DensityProfile");
    {
        const float AtFalloff = 0.02f * std::exp(-1.0f);
        Check(std::fabs(AtFalloff - 0.0073576f) < 1e-6f, "density at the falloff height is one e-fold down");
        Check(std::fabs(0.02f * std::exp(-2.0f) - 0.0027067f) < 1e-6f, "density at twice falloff is two e-folds down");
        Check(std::lround(std::exp(-1.0f) * 100.0f) == 37, "the handle label reads 37 % of the datum density");
    }

    Medium.Enabled = false;
    Medium.Density = 0.12f;
    Medium.Falloff = 1200.0f;
    Capture("DensityProfile-Authored");

    //-----------------------------------------------------------------------------------------------------
    // Volume shape — every form the select offers resolves to its own mesh and world bounds.
    //-----------------------------------------------------------------------------------------------------
    Shown = Subject::Volume;
    struct Entry { FogForm Form; const char* File; size_t Vertices; size_t Edges; size_t Faces; };
    const Entry Roster[] = {
        { FogForm::Box,      "VolumeShape-Box",      8,  12,  6 },
        { FogForm::Sphere,   "VolumeShape-Sphere",   312, 408, 288 },
        { FogForm::Diamond,  "VolumeShape-Diamond",  6,  12,  8 },
        { FogForm::Cylinder, "VolumeShape-Cylinder", 96, 144, 50 },
        { FogForm::Cone,     "VolumeShape-Cone",     49, 56,  48 },
        { FogForm::Prism,    "VolumeShape-Prism",    12, 18,  8 },
        { FogForm::Custom,   "VolumeShape-Custom",   10, 15,  7 },
    };
    for (const Entry& Item : Roster)
    {
        Shape = FogShape{};
        Shape.Form = Item.Form;
        const ShapeMesh Mesh = ConstructMesh(Shape);
        Check(Mesh.Vertices.size() == Item.Vertices, "the mesh carries the vertex count FogShape.js builds");
        Check(Mesh.Edges.size() == Item.Edges, "the mesh carries the edge count FogShape.js builds");
        Check(Mesh.Faces.size() == Item.Faces, "the mesh carries the face count FogShape.js builds");
        Capture(Item.File);
    }

    Shape = FogShape{};
    {
        const ShapeMesh Mesh = ConstructMesh(Shape);
        Check(std::fabs(Mesh.Low[0] + 100.0f) < 0.001f, "a 200 m box reaches -100 m on X");
        Check(std::fabs(Mesh.High[2] - 100.0f) < 0.001f, "a 200 m box reaches +100 m on Z");
        Check(std::fabs(Mesh.Size[1] - 200.0f) < 0.001f, "a 200 m box measures 200 m deep");
    }
    Check(ValidOutline(FogShape{}.Outline, 5), "the default custom outline is an ordered, non-crossing polygon");
    {
        FogShape Crossed;
        Crossed.Outline[0][0] = -100; Crossed.Outline[0][1] = -100;
        Crossed.Outline[1][0] =  100; Crossed.Outline[1][1] =  100;
        Crossed.Outline[2][0] =  100; Crossed.Outline[2][1] = -100;
        Crossed.Outline[3][0] = -100; Crossed.Outline[3][1] =  100;
        Check(!ValidOutline(Crossed.Outline, 4), "a self-crossing outline is rejected");
    }

    //-----------------------------------------------------------------------------------------------------
    // Interaction — the cards must author the medium, not merely illustrate it.
    //-----------------------------------------------------------------------------------------------------
    Editable = true;
    Shown = Subject::Profile;
    Medium = FogValues{};
    Medium.Kind = FogKind::Height;
    Medium.Enabled = true;
    DensitySlot = Medium.Density;
    FalloffSlot = Medium.Falloff;
    Height = 2600;
    Tick();
    const int Tall = static_cast<int>(Measured) + 2;
    Height = Tall;
    Tick();
    {
        bool Authored = false;
        for (int Y = 0; Y < Tall && !Authored; Y += 2)
        {
            IO.AddMousePosEvent(260.0f, float(Y));
            IO.AddMouseButtonEvent(0, false); Tick();
            IO.AddMouseButtonEvent(0, true);  Tick();
            IO.AddMouseButtonEvent(0, false); Tick();
            Authored = std::fabs(DensitySlot - 0.02f) > 0.0005f && std::fabs(FalloffSlot - 400.0f) > 1.0f;
        }
        Check(Authored, "pressing the profile authors Density and Falloff Height together");
        Check(FalloffSlot >= 10.0f && FalloffSlot <= 3000.0f, "the authored falloff stays inside 10 to 3000 m");
        Check(DensitySlot >= 0.0f && DensitySlot <= 0.2f, "the authored density stays inside 0 to 0.2 per metre");
    }

    Shown = Subject::Volume;
    Shape = FogShape{};
    Cycled = Shape;
    Height = 2600;
    Tick();
    const int Deep = static_cast<int>(Measured) + 2;
    Height = Deep;
    Tick();
    {
        bool Advanced = false;
        for (int Y = 0; Y < Deep && !Advanced; Y += 2)
        {
            IO.AddMousePosEvent(200.0f, float(Y));
            IO.AddMouseButtonEvent(0, false); Tick();
            IO.AddMouseButtonEvent(0, true);  Tick();
            IO.AddMouseButtonEvent(0, false); Tick();
            Advanced = Cycled.Form != Shape.Form;
        }
        Check(Advanced, "clicking the shape select advances to the next form");
        Check(Cycled.Form == FogForm::Sphere, "Box advances to Sphere, the next entry in FogShapes");
    }
    IO.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    ImGui::DestroyContext();
    std::printf("PASS %u checks: native fog beam chamber, density profile and volume shape for seven forms.\n",
                Checks);
    return 0;
}
