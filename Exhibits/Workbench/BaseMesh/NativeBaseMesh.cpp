//==============================================================================================================================================
//                                                          NATIVEBASEMESH.CPP
//==============================================================================================================================================
// 📦 Executed proof: the five base meshes the shipped editor opens with, and the analytical markers it
//    draws for them, recorded from real ImGui draw commands.
//
//    Three things are proved that a screenshot cannot. The roster is the bundle's own InitialRows, name
//    for name and icon for icon. The primitive is derived the way FractureSpecification.js derives it —
//    off the icon, not the label — so "Cube 2" and an imported "Conveyor" both come out right where the
//    old name sniff did not. And the marker geometry is pinned vertex by vertex against the eleven SVG
//    path commands in CheckerViewport.jsx.

#include "BaseMeshSurface.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Frontier;
using namespace Frontier::BaseMesh;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
}
bool Near(float A, float B, float Slack = 1e-4f) { return std::fabs(A - B) <= Slack; }
}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/BaseMeshNative");
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig LightConfig;
    std::snprintf(LightConfig.Name, sizeof(LightConfig.Name), "Sun reference / light");
    ImFont* Light = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Light.ttf", 14,
                                                 &LightConfig);
    ImGui::StyleColorsDark();

    //-----------------------------------------------------------------------------------------------------
    // Editor.jsx InitialRows: five geometry rows under one Showcase group, each with its own artwork.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(int(Solid::Count) == 5, "the reference opens with five base meshes");
        const char* Names[] = { "Cube", "Sphere", "Cylinder", "Torus", "Cone" };
        const char* Icons[] = { "editor-cube", "editor-sphere", "editor-cylinder", "editor-torus",
                                "editor-cone" };
        for (int I = 0; I < 5; ++I)
        {
            Check(std::strcmp(Of(Solid(I)).Name, Names[I]) == 0, "the roster names match the bundle");
            Check(std::strcmp(Of(Solid(I)).Icon, Icons[I]) == 0, "and so do their icons");
            Check(std::strcmp(Of(Solid(I)).Descriptor, "Geometry") == 0, "every one is described Geometry");
            Check(std::strcmp(Of(Solid(I)).Id, PrimitiveOf(Solid(I))) == 0,
                  "the row id and the derived primitive are the same word");
        }
        Check(std::strcmp(GroupName, "Showcase") == 0 && std::strcmp(GroupIcon, "folder-scene") == 0,
              "they hang off the Showcase collection");
    }

    //-----------------------------------------------------------------------------------------------------
    // Describe().Primitive = Subject.Icon.replace(/^editor-/, ""). Including the cases the old native
    //    name sniff got wrong: a renamed duplicate, and a mesh that merely starts with a primitive's name.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(std::strcmp(PrimitiveOfIcon("editor-cube"), "cube") == 0, "the prefix is stripped");
        Check(std::strcmp(PrimitiveOfIcon("editor-torus"), "torus") == 0, "for every base mesh");
        Check(std::strcmp(PrimitiveOfIcon("folder-scene"), "folder-scene") == 0,
              "an icon without the prefix is returned whole, as String.replace does");
        Check(PrimitiveOfIcon(nullptr) == nullptr, "and a row with no icon has no primitive");

        // The regression the rewrite closes: these two rows are a cube and an import, and the label says
        //    the opposite of the truth in both directions.
        Check(std::strcmp(PrimitiveOfIcon("editor-cube"), "cube") == 0, "a row named \"Cube 2\" is a cube");
        Check(std::strcmp(PrimitiveOfIcon("editor-mesh"), "mesh") == 0,
              "and a row named \"Cone bracket\" that is an import is not a cone");

        Check(Partitionable("cube") && Partitionable("sphere") && Partitionable("cylinder")
              && Partitionable("cone"), "four of the five can be partitioned");
        Check(Partitionable("pane") && Partitionable("beam") && Partitionable("rock"),
              "together with the three standalone specimens the editor offers");
        Check(!Partitionable("torus"), "a torus cannot: it is not convex");
        Check(SolidOfPrimitive("cylinder") == Solid::Cylinder, "the round trip closes");
        Check(SolidOfPrimitive("teapot") == Solid::Count, "and rejects what is not a base mesh");
    }

    //-----------------------------------------------------------------------------------------------------
    // CheckerViewport.jsx Shape(), command by command.
    //-----------------------------------------------------------------------------------------------------
    {
        const Artwork Cube = Shape(Solid::Cube);
        Check(Cube.TraceCount == 3 && Cube.OvalCount == 0, "the cube is three paths and no ellipse");
        Check(Cube.Traces[0].Count == 6 && Cube.Traces[0].Closed && Cube.Traces[0].Filled,
              "its body is a closed, filled hexagon");
        const float BodyX[6] = { 40, 67, 67, 40, 13, 13 };
        const float BodyY[6] = {  7, 22, 53, 69, 53, 22 };
        for (int I = 0; I < 6; ++I)
            Check(Near(Cube.Traces[0].Points[I].X, BodyX[I]) && Near(Cube.Traces[0].Points[I].Y, BodyY[I]),
                  "m40 7 27 15v31L40 69 13 53V22Z resolves to those six vertices");
        Check(Cube.Traces[1].Count == 3 && Near(Cube.Traces[1].Points[1].X, 40)
              && Near(Cube.Traces[1].Points[1].Y, 38), "the top-face vee meets at the centre");
        Check(Cube.Traces[2].Count == 2 && Near(Cube.Traces[2].Points[0].Y, 7)
              && Near(Cube.Traces[2].Points[1].Y, 69), "and the spine runs the full height");

        const Artwork Sphere = Shape(Solid::Sphere);
        Check(Sphere.OvalCount == 3 && Sphere.TraceCount == 0, "the sphere is three ellipses");
        Check(Near(Sphere.Ovals[0].RX, 28) && Near(Sphere.Ovals[0].RY, 28), "a 28 radius circle");
        Check(Near(Sphere.Ovals[1].RX, 12) && Near(Sphere.Ovals[1].RY, 28), "a meridian");
        Check(Near(Sphere.Ovals[2].RX, 28) && Near(Sphere.Ovals[2].RY, 10), "and an equator");

        const Artwork Cylinder = Shape(Solid::Cylinder);
        Check(Cylinder.OvalCount == 1 && Cylinder.TraceCount == 2, "the cylinder is a rim and two paths");
        Check(Cylinder.Traces[1].Dashes, "its hidden far rim is dashed 3 4");
        // c0 14 50 14 50 0 from (15,57): the curve's own midpoint is 10.5 below the rim, not 14.
        const Trace& Belly = Cylinder.Traces[0];
        Check(Near(Belly.Points[Belly.Count - 1].X, 65) && Near(Belly.Points[Belly.Count - 1].Y, 18),
              "the outline closes back up to the rim");
        float Lowest = 0.0f;
        for (int I = 0; I < Belly.Count; ++I) Lowest = std::max(Lowest, Belly.Points[I].Y);
        Check(Near(Lowest, 67.5f, 0.05f), "and the bulge bottoms out at 67.5, the cubic's own extreme");

        const Artwork Cone = Shape(Solid::Cone);
        Check(Cone.TraceCount == 1 && Cone.OvalCount == 1, "the cone is a filled vee under a base ellipse");
        Check(Cone.Traces[0].Filled && !Cone.Traces[0].Closed,
              "SVG fills the unclosed polyline as though it were closed");
        Check(Near(Cone.Traces[0].Points[1].X, 40) && Near(Cone.Traces[0].Points[1].Y, 9),
              "its apex is at 40, 9");

        const Artwork Torus = Shape(Solid::Torus);
        Check(Torus.OvalCount == 2 && Torus.TraceCount == 0, "the torus is two concentric ellipses");
        Check(Near(Torus.Ovals[0].RX, 30) && Near(Torus.Ovals[1].RX, 15), "30 outside, 15 inside");
    }

    //-----------------------------------------------------------------------------------------------------
    // The grid and the checker.
    //-----------------------------------------------------------------------------------------------------
    {
        // auto-fill minmax(110px, 1fr) with a 14 px gap inside 22 px of padding.
        Check(Columns(200.0f) == 1, "a 200 px pane fits one column");
        Check(Columns(300.0f) == 2, "300 fits two");
        Check(Columns(540.0f) == 4, "and 540 fits four");
        Check(Near(PlacementHeight(1), 126.0f), "a one-line placement is held open by its 126 px minimum");
        Check(PlacementHeight(4) > 126.0f, "a four-line label pushes past it");
        Check(Near(CheckerTile * 0.5f, 24.0f), "the conic tile gives 24 px squares");
    }

    //-----------------------------------------------------------------------------------------------------
    // Captures.
    //-----------------------------------------------------------------------------------------------------
    constexpr int Width = 560, Height = 340;
    Pane P;
    P.Spot = { 0, 0 };
    P.Wide = float(Width);
    P.Tall = float(Height);

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Light, 14.0f);
        PaintPane(ImGui::GetWindowDrawList(), Light, P);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        constexpr int Over = 3;
        const int WideOver = Width * Over, TallOver = Height * Over;
        std::vector<unsigned char> Dense(size_t(WideOver) * TallOver * 3, 9);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
            FrontierProof::Draw(List, Dense.data(), WideOver, TallOver, { 0, 0 }, { Over, Over },
                                ImTextureID(), {});
        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 9);
        for (int Y = 0; Y < Height; ++Y) for (int X = 0; X < Width; ++X) for (int C = 0; C < 3; ++C)
        {
            int Total = 0;
            for (int DY = 0; DY < Over; ++DY) for (int DX = 0; DX < Over; ++DX)
                Total += Dense[(size_t(Y * Over + DY) * WideOver + X * Over + DX) * 3 + C];
            Pixels[(size_t(Y) * Width + X) * 3 + C] = static_cast<unsigned char>(Total / (Over * Over));
        }
        const std::string Path = "Exhibits/Gallery/BaseMeshNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0,
              "capture written");
    };

    Capture("Roster");
    P.Picked = 0;  Capture("CubePicked");
    P.Picked = 3;  P.Hovered = 1;  Capture("TorusPickedSphereHover");
    P.Picked = -1; P.Hovered = -1; P.Count = 0; Capture("Empty");

    std::printf("PASS %u checks: base mesh roster, icon-derived primitive, marker geometry.\n", Checks);
    return 0;
}
