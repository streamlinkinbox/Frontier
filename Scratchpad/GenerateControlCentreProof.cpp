//============================================================================================================================================
//                                                GENERATECONTROLCENTREPROOF.CPP
//============================================================================================================================================
// 🧩 Headless proof of the Control Centre notch + drawer. Drives the REAL ControlCentreHost through scripted
//    InputExchange contacts (press → drag → release, taps, flings, horizontal slides), records it through the REAL
//    PixelSpace onto an ImGui context with no backend, then software-rasterises the ImGui draw lists to PNG.
//
//    Build (Linux / MSYS / WSL):
//      g++ -std=c++20 -O2 -DGLFW_INCLUDE_NONE -I. -IEngine -IExternalPackages/imgui -IExternalPackages/stb \
//          Scratchpad/GenerateControlCentreProof.cpp Engine/DisplayPresentation/ControlCentreHost.cpp \
//          Engine/DisplayPresentation/PixelSpace.cpp Engine/DisplayPresentation/MotionIntegrator.cpp \
//          Engine/DisplayPresentation/ThemeStructure.cpp Engine/DeviceExchange/InputExchange.cpp \
//          ExternalPackages/imgui/imgui.cpp ExternalPackages/imgui/imgui_draw.cpp ExternalPackages/imgui/imgui_tables.cpp \
//          ExternalPackages/imgui/imgui_widgets.cpp -o ControlCentreProof && ./ControlCentreProof
//
//    Output: Diagnostics/ControlCentre_Notch_*.png  (1280 × 720, scene stand-in = Cornell-box-ish gradient)

#include "../Engine/DisplayPresentation/ControlCentreHost.h"
#include "../Engine/DisplayPresentation/PixelSpace.h"
#include "../Engine/DeviceExchange/InputExchange.h"
#include "../Engine/DisplayPresentation/NotificationQueue.h"
#include "../Engine/DisplayPresentation/TelemetryMetrics.h"
#include "../Engine/DisplayPresentation/FidelityClassifier.h"
#include "../Engine/DisplayPresentation/TypefaceRegistry.h"
#include "../Engine/DisplayPresentation/ConfigurationRegistry.h"

#include <imgui.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int Width  = 1280;
constexpr int Height = 720;
constexpr float Step = 1.0f / 120.0f;   // [s] simulated frame

//------------------------------------------------------------------------------------------------------------------------
//                                              SOFTWARE RASTERISER FOR IMDRAWDATA
//------------------------------------------------------------------------------------------------------------------------

struct Canvas
{
    std::vector<float> Rgb;   // linear 0..1, 3 per pixel
    Canvas() : Rgb(static_cast<size_t>(Width) * Height * 3u, 0.0f) {}

    void Blend(int X, int Y, float R, float G, float B, float A)
    {
        if (X < 0 || Y < 0 || X >= Width || Y >= Height || A <= 0.0f) return;
        float* P = &Rgb[(static_cast<size_t>(Y) * Width + X) * 3u];
        P[0] = P[0] * (1.0f - A) + R * A;
        P[1] = P[1] * (1.0f - A) + G * A;
        P[2] = P[2] * (1.0f - A) + B * A;
    }
};

// Scene stand-in so the scrim is visible: a soft vertical gradient with a red / green band left / right,
//    echoing the Cornell box the overlay will sit on in Project-Zero.
void PaintSceneStandIn(Canvas& C)
{
    for (int Y = 0; Y < Height; ++Y)
        for (int X = 0; X < Width; ++X)
        {
            const float V = static_cast<float>(Y) / Height;
            float R = 0.16f + 0.10f * V, G = 0.17f + 0.10f * V, B = 0.20f + 0.10f * V;
            if (X < Width / 5)          { R = 0.55f; G = 0.14f; B = 0.14f; }
            else if (X > Width * 4 / 5) { R = 0.12f; G = 0.50f; B = 0.16f; }
            C.Blend(X, Y, R, G, B, 1.0f);
        }
}

// Textured-triangle rasteriser with barycentric interpolation and a pixel-centre coverage (ImGui geometry carries its own AA fringe).
void RasterTriangle(Canvas& C, const ImDrawVert& A, const ImDrawVert& B, const ImDrawVert& D,
                    const unsigned char* Tex, int TexW, int TexH, const ImVec4& Clip)
{
    const float MinX = std::max(Clip.x, std::floor(std::min({ A.pos.x, B.pos.x, D.pos.x })));
    const float MaxX = std::min(Clip.z, std::ceil (std::max({ A.pos.x, B.pos.x, D.pos.x })));
    const float MinY = std::max(Clip.y, std::floor(std::min({ A.pos.y, B.pos.y, D.pos.y })));
    const float MaxY = std::min(Clip.w, std::ceil (std::max({ A.pos.y, B.pos.y, D.pos.y })));
    if (MinX >= MaxX || MinY >= MaxY) return;

    const float Area = (B.pos.x - A.pos.x) * (D.pos.y - A.pos.y) - (B.pos.y - A.pos.y) * (D.pos.x - A.pos.x);
    if (std::fabs(Area) < 1e-6f) return;
    const float InvArea = 1.0f / Area;

    auto Unpack = [](ImU32 Col, float* Out)
    {
        Out[0] = ((Col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        Out[1] = ((Col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        Out[2] = ((Col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
        Out[3] = ((Col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    };
    float CA[4], CB[4], CD[4];
    Unpack(A.col, CA); Unpack(B.col, CB); Unpack(D.col, CD);

    constexpr int SS = 1;   // pixel-centre test, exactly like the GPU rasteriser (ImGui supplies its own AA fringe)
    for (int Y = static_cast<int>(MinY); Y < static_cast<int>(MaxY); ++Y)
        for (int X = static_cast<int>(MinX); X < static_cast<int>(MaxX); ++X)
        {
            float AccR = 0, AccG = 0, AccB = 0, AccA = 0; int Hits = 0;
            for (int SY = 0; SY < SS; ++SY)
                for (int SX = 0; SX < SS; ++SX)
                {
                    const float PX = X + (SX + 0.5f) / SS;
                    const float PY = Y + (SY + 0.5f) / SS;
                    float W0 = ((B.pos.x - PX) * (D.pos.y - PY) - (B.pos.y - PY) * (D.pos.x - PX)) * InvArea;
                    float W1 = ((D.pos.x - PX) * (A.pos.y - PY) - (D.pos.y - PY) * (A.pos.x - PX)) * InvArea;
                    float W2 = 1.0f - W0 - W1;
                    if (W0 < 0 || W1 < 0 || W2 < 0) continue;

                    float R = W0 * CA[0] + W1 * CB[0] + W2 * CD[0];
                    float G = W0 * CA[1] + W1 * CB[1] + W2 * CD[1];
                    float Bl= W0 * CA[2] + W1 * CB[2] + W2 * CD[2];
                    float Al= W0 * CA[3] + W1 * CB[3] + W2 * CD[3];

                    const float U = W0 * A.uv.x + W1 * B.uv.x + W2 * D.uv.x;
                    const float V = W0 * A.uv.y + W1 * B.uv.y + W2 * D.uv.y;
                    const int TX = std::clamp(static_cast<int>(U * TexW), 0, TexW - 1);
                    const int TY = std::clamp(static_cast<int>(V * TexH), 0, TexH - 1);
                    const unsigned char* T = &Tex[(static_cast<size_t>(TY) * TexW + TX) * 4u];
                    Al *= T[3] / 255.0f;

                    AccR += R * Al; AccG += G * Al; AccB += Bl * Al; AccA += Al; ++Hits;
                }
            if (Hits == 0 || AccA <= 0.0f) continue;
            const float Cov = AccA / (SS * SS);
            C.Blend(X, Y, AccR / AccA, AccG / AccA, AccB / AccA, Cov);
        }
}

void RasterDrawData(Canvas& C, ImDrawData* Data, const unsigned char* Tex, int TexW, int TexH)
{
    for (int L = 0; L < Data->CmdListsCount; ++L)
    {
        const ImDrawList* List = Data->CmdLists[L];
        const ImDrawVert* Vtx  = List->VtxBuffer.Data;
        const ImDrawIdx*  Idx  = List->IdxBuffer.Data;
        for (const ImDrawCmd& Cmd : List->CmdBuffer)
        {
            if (Cmd.UserCallback) continue;
            for (unsigned I = 0; I + 2 < Cmd.ElemCount; I += 3)
            {
                const ImDrawVert& A = Vtx[Cmd.VtxOffset + Idx[Cmd.IdxOffset + I + 0]];
                const ImDrawVert& B = Vtx[Cmd.VtxOffset + Idx[Cmd.IdxOffset + I + 1]];
                const ImDrawVert& D = Vtx[Cmd.VtxOffset + Idx[Cmd.IdxOffset + I + 2]];
                RasterTriangle(C, A, B, D, Tex, TexW, TexH, Cmd.ClipRect);
            }
        }
    }
}

void SavePng(const Canvas& C, const std::string& Path)
{
    std::vector<unsigned char> Bytes(static_cast<size_t>(Width) * Height * 3u);
    for (size_t I = 0; I < Bytes.size(); ++I)
        Bytes[I] = static_cast<unsigned char>(std::clamp(C.Rgb[I], 0.0f, 1.0f) * 255.0f + 0.5f);
    stbi_write_png(Path.c_str(), Width, Height, 3, Bytes.data(), Width * 3);
    std::printf("wrote %s\n", Path.c_str());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCRIPTED CONTACT
//------------------------------------------------------------------------------------------------------------------------

struct Rig
{
    Frontier::ControlCentreHost Host;
    Frontier::InputExchange     Input;
    Frontier::PixelSpace  Surface;
    Frontier::NotificationQueue Notifications;
    Frontier::TelemetryMetrics  Telemetry;
    uint32_t AppliedRevision = 0u;
    float CursorX = 640.0f, CursorY = 300.0f;
    const unsigned char* Tex = nullptr; int TexW = 0, TexH = 0;
    uint32_t FrameCounter = 0u;
    float PendingWheel = 0.0f;

    Rig()
    {
        Host.AssignProjectName("Project-Zero");
        (void)Host.Initialize(Width, Height);
    }

    // One simulated frame: interaction → locomotion → record → (optionally) rasterise.
    void Frame(Canvas* Out)
    {
        // Mirror GameExecution's Interface-scale mapping: the host runs in logical pixels; the script drives the cursor
        //    in logical pixels too (extents it queries are logical), so only the surface size is divided here.
        const float Scale = std::clamp(Host.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.5f, 2.0f);
        Host.Resize(static_cast<uint32_t>(Width / Scale + 0.5f), static_cast<uint32_t>(Height / Scale + 0.5f));
        Input.AssignCursorPosition(CursorX * Scale, CursorY * Scale);
        Input.ResetMouseScroll();
        if (PendingWheel != 0.0f) { Input.AssignMouseScroll(PendingWheel); PendingWheel = 0.0f; }
        Host.AdvanceInteraction(Input, CursorX, CursorY);
        Host.AdvanceLocomotion(Step);
        Notifications.Advance(Step);
        Telemetry.RecordFrame(Step * (1.0f + 0.3f * std::sin(static_cast<float>(FrameCounter++) * 0.21f)));   // wobble so the sparkline shows

        // Mirror GameExecution: a settings change raises a toast
        const Frontier::ControlCentreSettings& S = Host.QuerySettings();
        if (S.Revision != AppliedRevision)
        {
            Notifications.AssignEnabled(S.Notifications);
            char Body[96];
            std::snprintf(Body, sizeof(Body), "%s  |  GI %s, AA %s, scale %d%%", Frontier::FidelityLabel(S.Quality),
                          S.GlobalIllumination ? "on" : "off", S.AntiAliasing ? "on" : "off",
                          static_cast<int>(S.RenderScale * 100.0f + 0.5f));
            Notifications.Push("Render settings applied", Body);
            AppliedRevision = S.Revision;
        }

        ImGuiIO& IO = ImGui::GetIO();
        IO.DeltaTime   = Step;
        IO.DisplaySize = ImVec2(Width, Height);
        ImGui::NewFrame();
        if (Surface.Begin(Frontier::SurfaceLayer::Above, Width, Height, Scale))
        {
            const float NotchLine = Host.QueryHandleHeight();
            if (Host.QuerySettings().FrameRateOverlay) Telemetry.ConstructTelemetryLayout(Surface, NotchLine);
            Host.ConstructControlLayout(Surface);
            Notifications.ConstructNotificationLayout(Surface, NotchLine);
        }
        ImGui::Render();
        if (ImDrawData* D = ImGui::GetDrawData(); D && D->Textures)
            for (ImTextureData* T : *D->Textures) if (T->Status == ImTextureStatus_WantCreate || T->Status == ImTextureStatus_WantUpdates) { T->SetStatus(ImTextureStatus_OK); T->SetTexID(static_cast<ImTextureID>(1)); }

        if (Out)
        {
            PaintSceneStandIn(*Out);
            ImDrawData* Data = ImGui::GetDrawData();
            const unsigned char* AtlasPixels = Tex; int AtlasW = TexW, AtlasH = TexH;
            if (Data->Textures) for (ImTextureData* T : *Data->Textures) if (T->Status != ImTextureStatus_Destroyed && T->Pixels) { AtlasPixels = static_cast<const unsigned char*>(T->Pixels); AtlasW = T->Width; AtlasH = T->Height; T->SetStatus(ImTextureStatus_OK); T->SetTexID(static_cast<ImTextureID>(1)); }
            RasterDrawData(*Out, Data, AtlasPixels, AtlasW, AtlasH);
        }
    }

    void Idle(int Frames) { for (int I = 0; I < Frames; ++I) Frame(nullptr); }
    void Press()   { Input.AssignMouseButton(Frontier::MouseButtonCategory::ButtonLeft, true);  Frame(nullptr); }
    void Release() { Input.AssignMouseButton(Frontier::MouseButtonCategory::ButtonLeft, false); Frame(nullptr); }

    // Move the cursor to (X, Y) over N frames (linear), holding whatever button state is current.
    void Drag(float X, float Y, int Frames)
    {
        const float X0 = CursorX, Y0 = CursorY;
        for (int I = 1; I <= Frames; ++I)
        {
            const float T = static_cast<float>(I) / Frames;
            CursorX = X0 + (X - X0) * T;
            CursorY = Y0 + (Y - Y0) * T;
            Frame(nullptr);
        }
    }

    void Snapshot(const char* Name)
    {
        Canvas C;
        Frame(&C);
        SavePng(C, std::string("Diagnostics/") + Name + ".png");
        const Frontier::ControlCentreSettings& S = Host.QuerySettings();
        std::printf("   pose=%u shadeY=%.1f notchX=%.1f covers=%d hover=%d | GI=%d AA=%d FPS=%d Notif=%d Q=%s scale=%.2f rev=%u\n",
                    static_cast<unsigned>(Host.QueryPose()), Host.QueryCurrentHeight(), Host.QueryHandleX(),
                    Host.CoversPointer() ? 1 : 0, Host.QueryHoveredSlot(), S.GlobalIllumination, S.AntiAliasing,
                    S.FrameRateOverlay, S.Notifications, Frontier::FidelityLabel(S.Quality), S.RenderScale, S.Revision);
    }
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize = ImVec2(Width, Height);
    // Dynamic atlas (ImGuiBackendFlags_RendererHasTextures), exactly like the Vulkan backend: faces rasterise on demand
    //    at any size; the CPU rasteriser reads the atlas ImTextureData after each Render().
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    IO.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
    IO.Fonts->AddFontDefault();

    static Frontier::TypefaceRegistry Typefaces;
    const uint32_t FamilyCount = Typefaces.Load("EngineContent/FontArchives");
    Frontier::TypefaceRegistry::Install(&Typefaces);
    std::printf("typefaces: %u families\n", FamilyCount);

    Rig R;

    // ① Closed, idle.
    R.Idle(10);
    R.Snapshot("ControlCentre_Notch_01_Closed");

    // ② Press on the notch and drag down 180 px (past the 6 px tap ceiling, offset > 50 px → opens on release).
    R.CursorX = 640.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press();
    R.Drag(640.0f, 200.0f, 30);
    R.Snapshot("ControlCentre_Notch_02_Dragging_180px");

    // ③ Release: offset 182 px > 50 px → opens. Capture mid-spring and settled.
    R.Release();
    R.Idle(6);
    R.Snapshot("ControlCentre_Notch_03_Opening_Mid");
    R.Idle(120);
    R.Snapshot("ControlCentre_Notch_04_Open");

    // ④ From OPEN: drag the notch up 30 px then hold still (offset < 50, |vel| < 20) → release → stays open (Notch rule).
    R.CursorX = 640.0f; R.CursorY = Height - 18.0f; R.Idle(2);
    R.Press();
    R.Drag(640.0f, Height - 48.0f, 60);
    R.Snapshot("ControlCentre_Notch_05_OpenPulledUp30px_Dragging");
    R.Idle(30);                          // hold still so release velocity decays below 20 px/s
    R.Release();
    R.Idle(150);
    R.Snapshot("ControlCentre_Notch_06_StaysOpen");

    // ⑤ Tap on the scrim area (below the shade content region does not exist when fully open; tap the notch instead).
    R.CursorX = 640.0f; R.CursorY = Height - 18.0f; R.Idle(2);
    R.Press(); R.Idle(2); R.Release();
    R.Idle(150);
    R.Snapshot("ControlCentre_Notch_07_TapClosed");

    // ⑥ Horizontal: press on notch, drag 300 px to the right (axis resolves to X) → notch slides along the top edge.
    R.CursorX = 640.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press();
    R.Drag(940.0f, 22.0f, 30);
    R.Snapshot("ControlCentre_Notch_08_SlideRight_Dragging");
    R.Release();
    R.Idle(120);
    R.Snapshot("ControlCentre_Notch_09_SlideRight_Settled");

    // ⑦ Overshoot: drag far past the admissible band (elastic 5 %) then release → springs back to the bound.
    R.CursorX = R.Host.QueryHandleX() + 200.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press();
    R.Drag(R.CursorX + 900.0f, 18.0f, 30);
    R.Snapshot("ControlCentre_Notch_10_SlideOvershoot_Dragging");
    R.Release();
    R.Idle(120);
    R.Snapshot("ControlCentre_Notch_11_SlideOvershoot_Settled");

    // ⑧ From OPEN: fast upward flick of 100 px (vel < −20) → closes on velocity.
    R.CursorX = R.Host.QueryHandleX() + 200.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press(); R.Idle(2); R.Release();      // tap → open
    R.Idle(150);
    R.CursorX = R.Host.QueryHandleX() + 200.0f; R.CursorY = Height - 18.0f; R.Idle(2);
    R.Press();
    R.Drag(R.CursorX, Height - 118.0f, 4); // 100 px in 4 frames at 120 Hz ≈ 3000 px/s upward
    R.Release();
    R.Idle(10);
    R.Snapshot("ControlCentre_Notch_12_FlingClose_Mid");
    R.Idle(150);
    R.Snapshot("ControlCentre_Notch_13_FlingClosed");

    //------------------------------------------------------------------------------------------------------------------
    // Step 2 — dashboard. Open the drawer, then exercise every tile with real press/release contacts.
    //------------------------------------------------------------------------------------------------------------------
    auto Centre = [](const Frontier::PlaneExtent& E, float& X, float& Y) { X = (E.MinimumX + E.MaximumX) * 0.5f; Y = (E.MinimumY + E.MaximumY) * 0.5f; };
    auto TapSlot = [&](unsigned Slot)
    {
        Centre(R.Host.QueryTileDiscExtent(Slot), R.CursorX, R.CursorY);
        R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(2);
    };

    // Bring the notch back to centre and open by tap.
    R.CursorX = R.Host.QueryHandleX() + 200.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press(); R.Drag(640.0f, 18.0f, 30); R.Release(); R.Idle(120);
    R.CursorX = 640.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press(); R.Idle(2); R.Release();
    R.Idle(20);
    R.Snapshot("ControlCentre_Dashboard_01_Opening_CardFadingIn");
    R.Idle(150);
    R.Snapshot("ControlCentre_Dashboard_02_Open_Defaults");

    // Hover the Anti-Aliasing tile (active → hover blue-400).
    Centre(R.Host.QueryTileDiscExtent(1), R.CursorX, R.CursorY); R.Idle(3);
    R.Snapshot("ControlCentre_Dashboard_03_Hover_AntiAliasing");

    // Tap Global Illumination off → tile goes idle grey, toast appears.
    TapSlot(0);
    R.CursorX = 640.0f; R.CursorY = 300.0f; R.Idle(6);
    R.Snapshot("ControlCentre_Dashboard_04_GI_Off_Toast");

    // Tap FPS Overlay on → top-left readout appears.
    TapSlot(2);
    R.CursorX = 640.0f; R.CursorY = 300.0f; R.Idle(30);
    R.Snapshot("ControlCentre_Dashboard_05_FPS_Overlay_On");

    // Cycle Quality through every tier: Standard → Ultra → Reference → Minimal → Economy → Standard.
    const char* Names[] = { "Ultra", "Reference", "Minimal", "Economy", "Standard" };
    for (int I = 0; I < 5; ++I)
    {
        TapSlot(4);
        R.CursorX = 640.0f; R.CursorY = 300.0f; R.Idle(8);
        char Name[96]; std::snprintf(Name, sizeof(Name), "ControlCentre_Dashboard_06_Quality_%d_%s", I + 1, Names[I]);
        R.Snapshot(Name);
        R.Idle(500);   // let toasts expire between tiers so each frame shows one
    }

    // Drag the render-scale pill to about 60 %.
    {
        Frontier::PlaneExtent Track = R.Host.QueryPillTrackExtent();
        R.CursorX = Track.MaximumX - 2.0f; R.CursorY = (Track.MinimumY + Track.MaximumY) * 0.5f; R.Idle(2);
        R.Press();
        R.Drag(Track.MinimumX + Track.Width() * 0.4667f, R.CursorY, 40);
        R.Snapshot("ControlCentre_Dashboard_07_RenderScale_Dragging");
        R.Release(); R.Idle(6);
        R.Snapshot("ControlCentre_Dashboard_08_RenderScale_60pct");
    }

    // Notifications off, then toggle AA — no toast may appear.
    TapSlot(3);
    R.Idle(500);
    TapSlot(1);
    R.CursorX = 640.0f; R.CursorY = 300.0f; R.Idle(6);
    R.Snapshot("ControlCentre_Dashboard_09_Notifications_Off_NoToast");

    // Close by tapping the notch; the FPS overlay stays because it is a scene overlay, not part of the shade.
    R.CursorX = 640.0f; R.CursorY = Height - 18.0f; R.Idle(2);
    R.Press(); R.Idle(2); R.Release(); R.Idle(150);
    R.Snapshot("ControlCentre_Dashboard_10_Closed_FPS_Overlay_Persists");

    //--------------------------------------------------------------------------------------------------------------------
    //                                              STEP 3 · SETTINGS PAGES
    //--------------------------------------------------------------------------------------------------------------------

    auto TapExtent = [&](const Frontier::PlaneExtent& E)
    {
        R.CursorX = (E.MinimumX + E.MaximumX) * 0.5f; R.CursorY = (E.MinimumY + E.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Idle(3); R.Release();
    };
    auto Page = [&]{ return static_cast<unsigned>(R.Host.QueryActivePage()); };

    // Open the shade (tap the notch) and settle.
    R.CursorX = 640.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press(); R.Idle(2); R.Release(); R.Idle(150);

    // ⑪ Hover the gear, then tap it → hub slides in (capture mid-swap and settled).
    {
        const Frontier::PlaneExtent Gear = R.Host.QueryHeaderGearExtent();
        R.CursorX = (Gear.MinimumX + Gear.MaximumX) * 0.5f; R.CursorY = (Gear.MinimumY + Gear.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Idle(3); R.Release();
        R.Idle(18);  // swap ≈ 0.75: exit half done, entry half under way
        std::printf("   page=%u swap=%.2f slide=%.1f\n", Page(), R.Host.QueryPageSwapProgress(), R.Host.QuerySlideOffset());
        R.Snapshot("ControlCentre_Settings_11_Gear_Tapped_Hub_Entering");
        R.Idle(20);
        R.Snapshot("ControlCentre_Settings_12_Hub");
    }

    // ⑫ Hover the second row (Appearance) — hover fill visible.
    {
        const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(1u);
        R.CursorX = (Row.MinimumX + Row.MaximumX) * 0.5f; R.CursorY = (Row.MinimumY + Row.MaximumY) * 0.5f;
        R.Idle(3);
        std::printf("   hub hover row=%d\n", R.Host.QueryHoveredHubRow());
        R.Snapshot("ControlCentre_Settings_13_Hub_Hover_Appearance");
    }

    // ⑬ Press the Render Settings row (fires on pointer-down) → card springs 420×480 → 840×600 while the page enters.
    {
        const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(0u);
        R.CursorX = (Row.MinimumX + Row.MaximumX) * 0.5f; R.CursorY = (Row.MinimumY + Row.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Idle(1);
        R.Snapshot("ControlCentre_Settings_14_Render_Row_Pressed_Card_Growing");
        R.Idle(6); R.Release(); R.Idle(8);
        std::printf("   page=%u swap=%.2f slide=%.1f card=%.0fx%.0f\n", Page(), R.Host.QueryPageSwapProgress(), R.Host.QuerySlideOffset(), R.Host.QueryCardExtent().Width(), R.Host.QueryCardExtent().Height());
        R.Snapshot("ControlCentre_Settings_15_Render_Page_Entering");
        R.Idle(60);
        const Frontier::PlaneExtent Card = R.Host.QueryCardExtent();
        std::printf("   page=%u card=%.0fx%.0f\n", Page(), Card.Width(), Card.Height());
        R.Snapshot("ControlCentre_Settings_16_Render_Page");
    }

    // ⑭ X close → back to the hub, card springs back.
    TapExtent(R.Host.QueryPageCloseExtent());
    R.Idle(16);
    std::printf("   page=%u swap=%.2f card=%.0fx%.0f\n", Page(), R.Host.QueryPageSwapProgress(), R.Host.QueryCardExtent().Width(), R.Host.QueryCardExtent().Height());
    R.Snapshot("ControlCentre_Settings_17_Close_Card_Shrinking");
    R.Idle(60);
    std::printf("   page=%u\n", Page());

    // ⑮ Appearance page: tabs Display · Fonts · Theme (Fonts initial); tap Theme → underline moves, content slides.
    {
        const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(1u);
        R.CursorX = (Row.MinimumX + Row.MaximumX) * 0.5f; R.CursorY = (Row.MinimumY + Row.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(70);
        R.Snapshot("ControlCentre_Settings_18_Appearance_Page_Fonts_Tab");
        TapExtent(R.Host.QueryPageTabExtent(2u));
        R.Idle(4);
        std::printf("   page=%u tab=%u\n", Page(), static_cast<unsigned>(R.Host.QueryAppearanceSubTab()));
        R.Snapshot("ControlCentre_Settings_19_Appearance_Theme_Tab_Tapped");
        TapExtent(R.Host.QueryPageCloseExtent()); R.Idle(70);
    }

    // ⑯ Input page (Notch's own palette: #161415 sheet, #e254eb primary) and Notifications page.
    {
        const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(2u);
        R.CursorX = (Row.MinimumX + Row.MaximumX) * 0.5f; R.CursorY = (Row.MinimumY + Row.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(70);
        R.Snapshot("ControlCentre_Settings_20_Input_Page");
        // "Discard Changes" on the Input page calls onClose in Notch → hub.
        TapExtent(R.Host.QueryPageButtonExtent(false)); R.Idle(70);
        std::printf("   after Discard: page=%u\n", Page());

        const Frontier::PlaneExtent Row3 = R.Host.QueryHubRowExtent(3u);
        R.CursorX = (Row3.MinimumX + Row3.MaximumX) * 0.5f; R.CursorY = (Row3.MinimumY + Row3.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(70);
        R.Snapshot("ControlCentre_Settings_21_Notifications_Page");
    }

    // ⑰ Close the shade from a sub-page: 300 ms later the host has reset to the dashboard at 420×480.
    R.CursorX = 640.0f; R.CursorY = Height - 18.0f; R.Idle(2);
    R.Press(); R.Idle(2); R.Release(); R.Idle(150);
    std::printf("   after close: page=%u card=%.0fx%.0f\n", Page(), R.Host.QueryCardExtent().Width(), R.Host.QueryCardExtent().Height());
    R.CursorX = 640.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press(); R.Idle(2); R.Release(); R.Idle(150);
    R.Snapshot("ControlCentre_Settings_22_Reopened_Dashboard_Reset");

    //--------------------------------------------------------------------------------------------------------------------
    //                                     STEP 4 · DISPLAY / THEME CONTENT · DIRTY FOOTER · DIALOGUES
    //--------------------------------------------------------------------------------------------------------------------

    auto Focus = [&](const Frontier::PlaneExtent& E) { R.CursorX = (E.MinimumX + E.MaximumX) * 0.5f; R.CursorY = (E.MinimumY + E.MaximumY) * 0.5f; };
    auto Tap = [&](const Frontier::PlaneExtent& E) { Focus(E); R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(2); };
    auto Dirty = [&]{ return R.Host.QueryAppearance().IsDirty() ? 1 : 0; };
    auto Draft = [&]() -> const Frontier::AppearanceSettings& { return R.Host.QueryAppearance().QueryDraft(); };

    // Open shade → hub → Appearance (Fonts is initial) → Display tab.
    R.CursorX = 640.0f; R.CursorY = 18.0f; R.Idle(2);
    R.Press(); R.Idle(2); R.Release(); R.Idle(150);
    Tap(R.Host.QueryHeaderGearExtent()); R.Idle(30);
    { const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(1u); Focus(Row); R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(80); }
    Tap(R.Host.QueryPageTabExtent(0u)); R.Idle(30);
    std::printf("   [23] Display tab: dirty=%d\n", Dirty());
    R.Snapshot("ControlCentre_Settings_23_Display_Tab_Clean_Buttons_Disabled");

    // Drag the UI-scale slider to the right → draft changes, footer enables.
    {
        const Frontier::PlaneExtent S = R.Host.QueryAppearance().QueryScaleSliderExtent();
        R.CursorX = S.MinimumX + 13.0f + (S.Width() - 26.0f) * (50.0f / 150.0f); R.CursorY = (S.MinimumY + S.MaximumY) * 0.5f;   // ≈ current 100 %
        R.Idle(2); R.Press();
        R.Drag(S.MinimumX + S.Width() * 0.72f, R.CursorY, 20);
        std::printf("   [24] dragging UI scale: %.0f%% dirty=%d\n", Draft().InterfaceScale, Dirty());
        R.Snapshot("ControlCentre_Settings_24_Display_UIScale_Dragging_Footer_Enabled");
        R.Release(); R.Idle(4);
    }

    // Open the Resolution dropdown, pick 1080p.
    {
        Tap(R.Host.QueryAppearance().QueryResolutionDropdownExtent()); R.Idle(3);
        R.Snapshot("ControlCentre_Settings_25_Display_Resolution_Dropdown_Open");
        const Frontier::PlaneExtent Btn = R.Host.QueryAppearance().QueryResolutionDropdownExtent();
        const Frontier::PlaneExtent Menu = Frontier::ControlKit::DropdownMenuExtent(Btn, 4u);
        R.CursorX = Menu.MinimumX + 40.0f; R.CursorY = Menu.MinimumY + 6.0f + 2.0f * (Frontier::ControlKit::DropdownOptionHeight + 2.0f) + 18.0f;   // third option
        R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(3);
        std::printf("   [26] resolution=%u\n", static_cast<unsigned>(Draft().Resolution));
    }

    // Scroll the body down with the wheel.
    { const Frontier::PlaneExtent B = R.Host.QueryPageBodyExtent(); Focus(B); R.Idle(2); for (int I = 0; I < 6; ++I) { R.PendingWheel = -1.0f; R.Idle(1); } R.Idle(3); }
    std::printf("   [26] scroll=%.0f\n", R.Host.QueryBodyScroll());
    R.Snapshot("ControlCentre_Settings_26_Display_Scrolled");

    // Fullscreen switch + V-Sync segmented "Adaptive".
    // Presentation section lies below the fold: the extents above are only valid once scrolled into view.
    Tap(R.Host.QueryAppearance().QueryFullscreenSwitchExtent());
    {
        const Frontier::PlaneExtent Seg = R.Host.QueryAppearance().QueryVsyncSegmentExtent();
        R.CursorX = Seg.MaximumX - 30.0f; R.CursorY = (Seg.MinimumY + Seg.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(3);
    }
    std::printf("   [27] fullscreen=%d vsync=%u dirty=%d\n", Draft().Fullscreen ? 1 : 0, static_cast<unsigned>(Draft().VerticalSync), Dirty());
    R.Snapshot("ControlCentre_Settings_27_Display_Resolution_Fullscreen_VSync_Changed");

    // Discard → confirmation dialogue → Cancel keeps edits → Discard again → confirm → clean.
    Tap(R.Host.QueryPageButtonExtent(false)); R.Idle(30);
    std::printf("   [28] dialogue open=%d preset=%u\n", R.Host.IsDialogueOpen() ? 1 : 0, static_cast<unsigned>(R.Host.QueryDialogue().QueryActive()));
    R.Snapshot("ControlCentre_Settings_28_Discard_Dialogue");
    Tap(R.Host.QueryDialogue().QueryButtonExtent(Frontier::DialogueVerdictCategory::Cancel)); R.Idle(30);
    std::printf("   [28] after Cancel: dialogue=%d dirty=%d\n", R.Host.IsDialogueOpen() ? 1 : 0, Dirty());
    Tap(R.Host.QueryPageButtonExtent(false)); R.Idle(30);
    Tap(R.Host.QueryDialogue().QueryButtonExtent(Frontier::DialogueVerdictCategory::Primary)); R.Idle(30);
    std::printf("   [29] after Discard: dirty=%d scale=%.0f res=%u\n", Dirty(), Draft().InterfaceScale, static_cast<unsigned>(Draft().Resolution));
    R.Snapshot("ControlCentre_Settings_29_After_Discard_Clean");

    // Theme tab: select Nord, drag corner radius, pick Rose accent, pick a success swatch.
    Tap(R.Host.QueryPageTabExtent(2u)); R.Idle(30);
    R.Snapshot("ControlCentre_Settings_30_Theme_Tab");
    // Second tile row sits below the fold: scroll 3 clicks, then pick Nord.
    { const Frontier::PlaneExtent B = R.Host.QueryPageBodyExtent(); Focus(B); R.Idle(2); for (int I = 0; I < 3; ++I) { R.PendingWheel = -1.0f; R.Idle(1); } R.Idle(3); }
    Tap(R.Host.QueryAppearance().QueryThemeTileExtent(5u)); R.Idle(3);
    // Corner Radius card is the next one down: 4 more clicks bring it into the body.
    { const Frontier::PlaneExtent B = R.Host.QueryPageBodyExtent(); Focus(B); R.Idle(2); for (int I = 0; I < 4; ++I) { R.PendingWheel = -1.0f; R.Idle(1); } R.Idle(3); }
    {
        const Frontier::PlaneExtent S = R.Host.QueryAppearance().QueryRadiusSliderExtent();
        R.CursorX = S.MinimumX + 9.0f + (S.Width() - 18.0f) * 0.5f; R.CursorY = (S.MinimumY + S.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Drag(S.MinimumX + S.Width() * 0.92f, R.CursorY, 20);
        R.Snapshot("ControlCentre_Settings_31_Theme_Nord_Radius_Dragging");
        R.Release(); R.Idle(3);
    }
    // Accent section is below the fold: scroll it into view, then pick Rose (swatch 9).
    { const Frontier::PlaneExtent B = R.Host.QueryPageBodyExtent(); Focus(B); R.Idle(2); for (int I = 0; I < 3; ++I) { R.PendingWheel = -1.0f; R.Idle(1); } R.Idle(3); }
    Tap(R.Host.QueryAppearance().QueryAccentSwatchExtent(9u)); R.Idle(3);
    std::printf("   [32] theme=%u radius=%.0f accent=%u dirty=%d\n", static_cast<unsigned>(Draft().Theme), Draft().CornerRadius, static_cast<unsigned>(Draft().Accent), Dirty());
    R.Snapshot("ControlCentre_Settings_32_Theme_Changed_Footer_Enabled");

    // Scroll to the semantic rows.
    { const Frontier::PlaneExtent B = R.Host.QueryPageBodyExtent(); Focus(B); R.Idle(2); for (int I = 0; I < 7; ++I) { R.PendingWheel = -1.0f; R.Idle(1); } R.Idle(3); }
    R.Snapshot("ControlCentre_Settings_33_Theme_Semantic_Colours");

    // X with unsaved edits → Unsaved-changes dialogue; choose Apply → applied + back to hub.
    Tap(R.Host.QueryPageCloseExtent()); R.Idle(30);
    std::printf("   [34] close requested: dialogue=%d preset=%u page=%u\n", R.Host.IsDialogueOpen() ? 1 : 0, static_cast<unsigned>(R.Host.QueryDialogue().QueryActive()), static_cast<unsigned>(R.Host.QueryActivePage()));
    R.Snapshot("ControlCentre_Settings_34_Unsaved_Changes_Dialogue");
    Tap(R.Host.QueryDialogue().QueryButtonExtent(Frontier::DialogueVerdictCategory::Primary)); R.Idle(80);
    {
        const Frontier::AppearanceSettings& A = R.Host.QueryAppearance().QueryApplied();
        std::printf("   [35] after Apply: page=%u applied theme=%u radius=%.0f accent=%u dirty=%d rev=%u\n", static_cast<unsigned>(R.Host.QueryActivePage()),
                    static_cast<unsigned>(A.Theme), A.CornerRadius, static_cast<unsigned>(A.Accent), Dirty(), R.Host.QueryAppearance().QueryRevision());
    }
    R.Snapshot("ControlCentre_Settings_35_Applied_Back_On_Hub");

    // Re-enter Appearance: Theme tab shows the applied state, buttons disabled again.
    { const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(1u); Focus(Row); R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(80); }
    R.Snapshot("ControlCentre_Settings_36_Reentered_Theme_Applied_Clean");


    //--------------------------------------------------------------------------------------------------------------------
    //                                      STEP 4b · FONTS TAB
    //--------------------------------------------------------------------------------------------------------------------
    auto Wheel = [&](int Clicks) { const Frontier::PlaneExtent B = R.Host.QueryPageBodyExtent(); Focus(B); R.Idle(2); const float Dir = Clicks < 0 ? 1.0f : -1.0f; for (int I = 0; I < std::abs(Clicks); ++I) { R.PendingWheel = Dir; R.Idle(1); } R.Idle(3); };
    const Frontier::AppearanceInspector& AI = R.Host.QueryAppearance();

    Tap(R.Host.QueryPageTabExtent(1u)); R.Idle(30);
    std::printf("   [37] Fonts tab: family=%u dirty=%d\n", Draft().FontFamily, Dirty());
    R.Snapshot("ControlCentre_Settings_37_Fonts_Tab_Clean");

    // › scrolls the strip by 300 px (smooth); capture mid-flight and settled.
    Tap(AI.QueryFontStripButtonExtent(true)); R.Idle(2);
    R.Snapshot("ControlCentre_Settings_38_Fonts_Strip_Scrolling");
    R.Idle(30);
    Tap(AI.QueryFontStripButtonExtent(true)); R.Idle(30);
    std::printf("   [39] strip scroll=%.0f\n", AI.QueryFontStripScroll());

    // Pick Clash Display (tile 4) — visible after scrolling.
    Tap(AI.QueryFontCardExtent(4u)); R.Idle(3);
    std::printf("   [39] family=%u dirty=%d\n", Draft().FontFamily, Dirty());
    R.Snapshot("ControlCentre_Settings_39_Fonts_ClashDisplay_Picked");

    // Scroll to the playground + Title card, drag Title size to ~48, pick SemiBold.
    Wheel(6);
    R.Snapshot("ControlCentre_Settings_40_Fonts_Playground");
    Wheel(6);
    {
        const Frontier::PlaneExtent S = AI.QueryRoleSliderExtent(0u);
        R.CursorX = S.MinimumX + 9.0f + (S.Width() - 18.0f) * ((32.0f - 8.0f) / 64.0f); R.CursorY = (S.MinimumY + S.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Drag(S.MinimumX + 9.0f + (S.Width() - 18.0f) * ((48.0f - 8.0f) / 64.0f), R.CursorY, 20);
        std::printf("   [41] dragging Title size=%.0f\n", Draft().RoleSize[0]);
        R.Snapshot("ControlCentre_Settings_41_Fonts_Title_Size_Dragging");
        R.Release(); R.Idle(3);
    }
    Tap(AI.QueryRoleWeightChipExtent(0u, Frontier::FontWeightCategory::SemiBold)); R.Idle(3);
    std::printf("   [42] Title weight=%u size=%.0f dirty=%d\n", static_cast<unsigned>(Draft().RoleWeight[0]), Draft().RoleSize[0], Dirty());
    R.Snapshot("ControlCentre_Settings_42_Fonts_Title_SemiBold");

    // Switch family to Space Grotesk (5 weights) — the chip row shrinks to what the family ships.
    Wheel(-12);
    Tap(AI.QueryFontStripButtonExtent(false)); R.Idle(30); Tap(AI.QueryFontStripButtonExtent(false)); R.Idle(30);   // ‹ ‹ back to the start
    Tap(AI.QueryFontCardExtent(2u)); R.Idle(3);   // Space Grotesk (slot 2 with General Sans absent)
    Wheel(12);
    std::printf("   [43] family=%u Title weight=%u (snapped)\n", Draft().FontFamily, static_cast<unsigned>(Draft().RoleWeight[0]));
    R.Snapshot("ControlCentre_Settings_43_Fonts_SpaceGrotesk_Weights_PerFamily");

    // Font Rendering: scroll to the end, toggle Ligatures off.
    Wheel(40);
    Tap(AI.QueryFontSwitchExtent(true)); R.Idle(3);
    std::printf("   [44] ligatures=%d aa=%d\n", Draft().Ligatures ? 1 : 0, Draft().FontAntialiasing ? 1 : 0);
    R.Snapshot("ControlCentre_Settings_44_Fonts_Rendering_Ligatures_Off");

    // Apply → whole Control Centre chrome re-renders in the applied family.
    Tap(R.Host.QueryPageButtonExtent(true)); R.Idle(30);
    std::printf("   [45] applied family=%u dirty=%d rev=%u\n", R.Host.QueryAppearance().QueryApplied().FontFamily, Dirty(), R.Host.QueryAppearance().QueryRevision());
    R.Snapshot("ControlCentre_Settings_45_Fonts_Applied_Chrome_Refaced");
    Tap(R.Host.QueryPageCloseExtent()); R.Idle(80);
    R.Snapshot("ControlCentre_Settings_46_Hub_In_Applied_Typeface");

    // Step 5B — the applied theme recolours everything: hub/dashboard under Nord/Rose, then Light applied → page, hub, dashboard.
    Tap(R.Host.QueryHubBackExtent()); R.Idle(80);
    R.Snapshot("ControlCentre_Settings_47_Dashboard_Nord_Rose_Themed");
    Tap(R.Host.QueryHeaderGearExtent()); R.Idle(30);
    { const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(1u); Focus(Row); R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(80); }
    Tap(R.Host.QueryPageTabExtent(2u)); R.Idle(30);
    Tap(R.Host.QueryAppearance().QueryThemeTileExtent(2u)); R.Idle(3);
    { const Frontier::PlaneExtent B = R.Host.QueryPageBodyExtent(); Focus(B); R.Idle(2); for (int I = 0; I < 7; ++I) { R.PendingWheel = -1.0f; R.Idle(1); } R.Idle(3); }
    Tap(R.Host.QueryAppearance().QueryAccentSwatchExtent(6u)); R.Idle(3);
    std::printf("   [48] draft theme=%u accent=%u dirty=%d\n", static_cast<unsigned>(Draft().Theme), static_cast<unsigned>(Draft().Accent), Dirty());
    R.Snapshot("ControlCentre_Settings_48_Theme_Light_Draft_Not_Applied");
    Tap(R.Host.QueryPageButtonExtent(true)); R.Idle(30);   // Apply commits directly (Step 4 behaviour)
    std::printf("   [49] applied theme=%u accent=%u dirty=%d\n", static_cast<unsigned>(R.Host.QueryAppearance().QueryApplied().Theme), static_cast<unsigned>(R.Host.QueryAppearance().QueryApplied().Accent), Dirty());
    R.Snapshot("ControlCentre_Settings_49_Theme_Light_Applied_Page");
    Tap(R.Host.QueryPageCloseExtent()); R.Idle(80);
    R.Snapshot("ControlCentre_Settings_50_Hub_Light_Themed");
    Tap(R.Host.QueryHubBackExtent()); R.Idle(80);
    R.Snapshot("ControlCentre_Settings_51_Dashboard_Light_Themed");

    // Step 5D — Interface scale: Display tab → UI Scale slider dragged to ~125 %, applied; chrome re-lays out larger
    //    (logical canvas shrinks, primitives are emitted ×1.25) while the pointer mapping keeps hit-testing exact.
    Tap(R.Host.QueryHeaderGearExtent()); R.Idle(30);
    { const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(1u); Focus(Row); R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(80); }
    Tap(R.Host.QueryPageTabExtent(0u)); R.Idle(30);
    {
        const Frontier::PlaneExtent S = R.Host.QueryAppearance().QueryScaleSliderExtent();
        R.CursorX = S.MinimumX + 9.0f + (S.Width() - 18.0f) * 0.5f; R.CursorY = (S.MinimumY + S.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Drag(S.MinimumX + 9.0f + (S.Width() - 18.0f) * 0.75f, R.CursorY, 20); R.Release(); R.Idle(3);
    }
    std::printf("   [52] draft UI scale=%.0f%%\n", Draft().InterfaceScale);
    R.Snapshot("ControlCentre_Settings_52_Display_UIScale_Draft");
    Tap(R.Host.QueryPageButtonExtent(true)); R.Idle(30);
    std::printf("   [53] applied UI scale=%.0f%% logical=%ux%u\n", R.Host.QueryAppearance().QueryApplied().InterfaceScale, R.Host.QueryDisplayWidth(), R.Host.QueryDisplayHeight());
    R.Snapshot("ControlCentre_Settings_53_Display_UIScale_Applied_Page_Larger");
    Tap(R.Host.QueryPageCloseExtent()); R.Idle(80);
    Tap(R.Host.QueryHubBackExtent()); R.Idle(80);
    R.Snapshot("ControlCentre_Settings_54_Dashboard_UIScale_Applied");
    // Tiles still hit-test correctly under the scale: tap GI (slot 0) and confirm it toggles.
    { const bool Before = R.Host.QuerySettings().GlobalIllumination; Tap(R.Host.QueryTileDiscExtent(0u)); R.Idle(3);
      std::printf("   [55] GI %d -> %d under UI scale (hit-test exact)\n", Before, R.Host.QuerySettings().GlobalIllumination); }
    R.Snapshot("ControlCentre_Settings_55_Dashboard_UIScale_Tile_Tapped");

    // Step 5A — persistence round trip: applied state → TOML → fresh host seeded from it (textual proof).
    {
        Frontier::SlateConfiguration P;
        P.Render     = R.Host.QuerySettings();
        P.Appearance = R.Host.QueryAppearance().QueryApplied();
        const std::string Toml = Frontier::ConfigurationRegistry::Serialise(P);
        Frontier::SlateConfiguration Q; std::string Error;
        const bool Parsed = Frontier::ConfigurationRegistry::Deserialise(Toml, Q, &Error);
        std::printf("   [5A] toml=%zu bytes parsed=%d appearance-equal=%d render-equal=%d %s\n", Toml.size(), Parsed,
                    Q.Appearance == P.Appearance,
                    Q.Render.GlobalIllumination == P.Render.GlobalIllumination && Q.Render.Quality == P.Render.Quality
                        && Q.Render.RenderScale == P.Render.RenderScale && Q.Render.Notifications == P.Render.Notifications, Error.c_str());
        std::FILE* F = std::fopen("Scratchpad/ControlCentre_Settings_5A_Slate.config.toml", "wb");
        if (F) { std::fwrite(Toml.data(), 1, Toml.size(), F); std::fclose(F); }
    }


    //--------------------------------------------------------------------------------------------------------------------
    //                                   STEP 5E · INPUT PAGE · NOTIFICATIONS PAGE · RESET DEFAULTS
    //--------------------------------------------------------------------------------------------------------------------
    // Reseed Appearance to defaults (start-up path) so the 5E frames are captured at 100 % scale on the dark theme.
    R.Host.AccessAppearance().Seed(Frontier::AppearanceSettings{}); R.Idle(60);
    std::printf("   [5E] reseeded: logical=%ux%u page=%u gear=(%.0f,%.0f)\n", R.Host.QueryDisplayWidth(), R.Host.QueryDisplayHeight(), Page(), R.Host.QueryHeaderGearExtent().MinimumX, R.Host.QueryHeaderGearExtent().MinimumY);
    auto InDirty = [&]{ return R.Host.QueryInput().IsDirty() ? 1 : 0; };
    auto InDraft = [&]() -> const Frontier::InputPreferences& { return R.Host.QueryInput().QueryDraft(); };

    // Hub → Input (row 2).
    Tap(R.Host.QueryHeaderGearExtent()); R.Idle(30);
    std::printf("   [5E] after gear: page=%u\n", Page());
    { const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(2u); Focus(Row); R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(80); }
    std::printf("   [56] Input page: page=%u dirty=%d default=%d\n", Page(), InDirty(), R.Host.QueryInput().IsDefault());
    R.Snapshot("ControlCentre_Settings_56_Input_Clean_Buttons_Disabled");

    // Preset profile dropdown: open, capture, pick "Unreal Engine".
    Tap(R.Host.QueryInput().QueryProfileDropdownExtent()); R.Idle(3);
    std::printf("   [57] profile menu open=%d\n", R.Host.QueryInput().HasOpenMenu());
    R.Snapshot("ControlCentre_Settings_57_Input_Profile_Menu_Open");
    { const Frontier::PlaneExtent B = R.Host.QueryInput().QueryProfileDropdownExtent(); Tap(Frontier::Spanning(B.MinimumX, B.MaximumY + 6.0f + 4.0f + 40.0f * 2.0f, B.Width(), 40.0f)); R.Idle(3); }
    // Mouse sensitivity: drag from 50 % to ~80 %.
    {
        const Frontier::PlaneExtent S = R.Host.QueryInput().QuerySensitivitySliderExtent();
        R.CursorX = S.MinimumX + S.Width() * 0.5f; R.CursorY = (S.MinimumY + S.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Drag(S.MinimumX + S.Width() * 0.8f, R.CursorY, 20); R.Release(); R.Idle(3);
    }
    std::printf("   [58] draft profile=%u sensitivity=%.0f%% dirty=%d\n", static_cast<unsigned>(InDraft().Profile), InDraft().MouseSensitivity, InDirty());
    R.Snapshot("ControlCentre_Settings_58_Input_Profile_Sensitivity_Draft");

    // Toggles: Custom Shortcuts off (fields go read-only); scroll the body down; Advanced on, Invert Y-Axis on.
    Tap(R.Host.QueryInput().QueryCustomShortcutsSwitchExtent()); R.Idle(3);
    auto ScrollBody = [&](int Clicks) { const Frontier::PlaneExtent B = R.Host.QueryPageBodyExtent(); Focus(B); R.Idle(2); for (int I = 0; I < std::abs(Clicks); ++I) { R.PendingWheel = Clicks < 0 ? 1.0f : -1.0f; R.Idle(1); } R.Idle(5); };
    ScrollBody(10);
    Tap(R.Host.QueryInput().QueryAdvancedSwitchExtent()); R.Idle(3);
    Tap(R.Host.QueryInput().QueryInvertSwitchExtent()); R.Idle(3);
    std::printf("   [59] custom=%d advanced=%d invert=%d dirty=%d\n", InDraft().CustomShortcuts, InDraft().AdvancedControls, InDraft().InvertPitch, InDirty());
    R.Snapshot("ControlCentre_Settings_59_Input_Toggles_Draft");

    // Reset Defaults → ResetDefaults dialogue; cancel keeps the draft; confirm resets it.
    Tap(R.Host.QueryPageResetExtent()); R.Idle(20);
    std::printf("   [60] dialogue open=%d preset=%u\n", R.Host.IsDialogueOpen(), static_cast<unsigned>(R.Host.QueryDialogue().QueryActive()));
    R.Snapshot("ControlCentre_Settings_60_Input_Reset_Defaults_Dialogue");
    Tap(R.Host.QueryDialogue().QueryButtonExtent(Frontier::DialogueVerdictCategory::Cancel)); R.Idle(30);   // Cancel
    std::printf("   [60b] after cancel: sensitivity=%.0f%% dirty=%d\n", InDraft().MouseSensitivity, InDirty());
    Tap(R.Host.QueryPageResetExtent()); R.Idle(20);
    Tap(R.Host.QueryDialogue().QueryButtonExtent(Frontier::DialogueVerdictCategory::Primary)); R.Idle(30);   // Confirm
    std::printf("   [61] after reset: sensitivity=%.0f%% profile=%u default=%d dirty=%d\n", InDraft().MouseSensitivity, static_cast<unsigned>(InDraft().Profile), R.Host.QueryInput().IsDefault(), InDirty());
    R.Snapshot("ControlCentre_Settings_61_Input_After_Reset_Defaults");

    // Change again → Save keybindings commits; then a further change → Discard Changes asks (ConfirmDiscard).
    ScrollBody(10);
    Tap(R.Host.QueryInput().QueryInvertSwitchExtent()); R.Idle(3);
    Tap(R.Host.QueryPageButtonExtent(true)); R.Idle(30);
    std::printf("   [62] applied invert=%d rev=%u dirty=%d\n", R.Host.QueryInput().QueryApplied().InvertPitch, R.Host.QueryInput().QueryRevision(), InDirty());
    R.Snapshot("ControlCentre_Settings_62_Input_Saved_Applied");
    ScrollBody(-10);
    Tap(R.Host.QueryInput().QueryCustomShortcutsSwitchExtent()); R.Idle(3);
    Tap(R.Host.QueryPageButtonExtent(false)); R.Idle(20);
    std::printf("   [63] discard dialogue open=%d preset=%u\n", R.Host.IsDialogueOpen(), static_cast<unsigned>(R.Host.QueryDialogue().QueryActive()));
    R.Snapshot("ControlCentre_Settings_63_Input_Discard_Dialogue");
    Tap(R.Host.QueryDialogue().QueryButtonExtent(Frontier::DialogueVerdictCategory::Primary)); R.Idle(30);
    std::printf("   [63b] after discard: custom=%d dirty=%d\n", InDraft().CustomShortcuts, InDirty());

    // Notifications page (hub row 3).
    Tap(R.Host.QueryPageCloseExtent()); R.Idle(80);
    { const Frontier::PlaneExtent Row = R.Host.QueryHubRowExtent(3u); Focus(Row); R.Idle(2); R.Press(); R.Idle(3); R.Release(); R.Idle(80); }
    auto NoDirty = [&]{ return R.Host.QueryNotifications().IsDirty() ? 1 : 0; };
    auto NoDraft = [&]() -> const Frontier::NotificationPreferences& { return R.Host.QueryNotifications().QueryDraft(); };
    std::printf("   [64] Notifications page: page=%u dirty=%d\n", Page(), NoDirty());
    R.Snapshot("ControlCentre_Settings_64_Notifications_Clean_Buttons_Disabled");
    Tap(R.Host.QueryNotifications().QueryToggleExtent(2u)); R.Idle(3);   // Scene Metadata on
    Tap(R.Host.QueryNotifications().QueryToggleExtent(0u)); R.Idle(3);   // FPS overlay off
    ScrollBody(10);
    Tap(R.Host.QueryNotifications().QueryToggleExtent(6u)); R.Idle(3);   // Frame-rate drops on
    {
        const Frontier::PlaneExtent S = R.Host.QueryNotifications().QueryHoldSliderExtent();
        const float T0 = (3.5f - 1.0f) / 9.0f;
        R.CursorX = S.MinimumX + 9.0f + (S.Width() - 18.0f) * T0; R.CursorY = (S.MinimumY + S.MaximumY) * 0.5f;
        R.Idle(2); R.Press(); R.Drag(S.MinimumX + 9.0f + (S.Width() - 18.0f) * 0.6f, R.CursorY, 20); R.Release(); R.Idle(3);
    }
    std::printf("   [65] draft fps=%d ram=%d scene=%d drops=%d hold=%.1f dirty=%d\n", NoDraft().ShowFrameRateOverlay, NoDraft().ShowMemoryUsage, NoDraft().ShowSceneMetadata, NoDraft().FrameRateDrops, NoDraft().HoldSeconds, NoDirty());
    R.Snapshot("ControlCentre_Settings_65_Notifications_Draft_Footer_Enabled");
    Tap(R.Host.QueryPageButtonExtent(true)); R.Idle(30);
    std::printf("   [66] applied hold=%.1f fps-tile=%d dirty=%d\n", R.Host.QueryNotifications().QueryApplied().HoldSeconds, R.Host.QuerySettings().FrameRateOverlay, NoDirty());
    R.Snapshot("ControlCentre_Settings_66_Notifications_Saved_Applied");
    // Dashboard FPS tile mirrors the page flag.
    Tap(R.Host.QueryPageCloseExtent()); R.Idle(80);
    Tap(R.Host.QueryHubBackExtent()); R.Idle(80);
    R.Snapshot("ControlCentre_Settings_67_Dashboard_FPS_Tile_Mirrors_Notifications");

    // 5E persistence: Input + Notifications round-trip through TOML.
    {
        Frontier::SlateConfiguration P;
        P.Input         = R.Host.QueryInput().QueryApplied();
        P.Notifications = R.Host.QueryNotifications().QueryApplied();
        const std::string Toml = Frontier::ConfigurationRegistry::Serialise(P);
        Frontier::SlateConfiguration Q; std::string Error;
        const bool Parsed = Frontier::ConfigurationRegistry::Deserialise(Toml, Q, &Error);
        std::printf("   [5E] toml parsed=%d input-equal=%d notifications-equal=%d %s\n", Parsed, Q.Input == P.Input, Q.Notifications == P.Notifications, Error.c_str());
        std::FILE* F = std::fopen("Scratchpad/ControlCentre_Settings_5E_Slate.config.toml", "wb");
        if (F) { std::fwrite(Toml.data(), 1, Toml.size(), F); std::fclose(F); }
    }

    ImGui::DestroyContext();
    return 0;
}
