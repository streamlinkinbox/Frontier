/*==========================================================================================================================================
                                               MATERIALPREVIEWCONFIGURATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify inspection bounds, separated geometry, real zoom-pill input and bounded persistent diagnostics.

#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include "Engine/Render/MaterialPreviewGeometry.h"
#include "Engine/Render/SurfaceMaterial.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
#include <limits>

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition, const char *Description)
        {
            if (!Condition) throw std::runtime_error(Description);
            ++Checks;
        };
        Confirm(Count == 2, "Expected JSON report destination.");
        MaterialPreviewConfiguration Inspection;
        Confirm(ValidateMaterialPreviewConfiguration(Inspection), "Default inspection configuration rejected.");
        for (uint32_t Shape = 0; Shape <= 4; ++Shape)
            for (uint32_t Environment = 0; Environment <= 3; ++Environment)
            {
                Inspection.Shape = MaterialPreviewShape(Shape);
                Inspection.Environment = Environment;
                Confirm(ValidateMaterialPreviewConfiguration(Inspection), "Supported inspection preset rejected.");
            }
        Inspection.Zoom = std::numeric_limits<float>::quiet_NaN();
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "NaN zoom accepted.");
        Inspection = {};
        Inspection.Zoom = .49f;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Out-of-range zoom accepted.");
        Inspection = {};
        Inspection.Zoom = 5;
        Confirm(ValidateMaterialPreviewConfiguration(Inspection), "500% preview zoom rejected.");
        Inspection.Zoom = 5.01f;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Zoom above the 500% limit accepted.");
        Inspection = {};
        Inspection.Environment = 64;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Out-of-capacity environment accepted.");
        Inspection = {};
        Inspection.Environment = 12;
        Confirm(ValidateMaterialPreviewConfiguration(Inspection), "Imported environment index rejected.");
        {
            // 📝 Pins the 2026-10-22 Windows freeze: PreviewConfiguration[2] carries the
            // environment archive layer, so the dormant shader contract must accept non-zero
            // layers up to catalogue capacity and reject beyond it.
            MaterialScattering Contract{};
            Contract.Illumination.Configuration = {1, 0, 1, 0};
            Contract.Illumination.ViewFromSurface[15] = 1;
            Contract.Illumination.LightDirectionIntensity = {0, 1, 0, 1};
            Contract.Illumination.EnvironmentUp = {0, 1, 0, 0};
            Contract.Illumination.Strata[0].AlbedoRoughness = {.5f, .5f, .5f, .5f};
            Contract.Illumination.Strata[0].Optics = {0, 1.5f, 1, 0};
            Contract.PreviewConfiguration = {1, 0, 0, 0};
            Confirm(ValidateMaterialScattering(Contract), "Default-environment scattering contract rejected.");
            Contract.PreviewConfiguration = {1, 0, 3, 0};
            Confirm(ValidateMaterialScattering(Contract), "Environment layer 3 rejected by the dormant shader contract.");
            Contract.PreviewConfiguration = {1, 0, 63, 0};
            Confirm(ValidateMaterialScattering(Contract), "Environment layer at catalogue capacity rejected.");
            Contract.PreviewConfiguration = {1, 0, 64, 0};
            Confirm(!ValidateMaterialScattering(Contract), "Environment layer above catalogue capacity accepted.");
        }
        Inspection = {};
        Inspection.LightIntensity = 4;
        Confirm(ValidateMaterialPreviewConfiguration(Inspection), "Maximum light intensity rejected.");
        Inspection.LightIntensity = 4.01f;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Excessive light intensity accepted.");
        Inspection = {};
        Inspection.Shape = MaterialPreviewShape(5);
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Unknown shape accepted.");
        // 🧩 The renderer maps each shape to preview slot shape+1; slot 5 is
        // the draped cloth, and the scattering contract must accept every
        // slot the UI can select or selection dies as a severe failure.
        MaterialScattering Slots;
        for (uint32_t Shape = 0; Shape <= 4; ++Shape)
        {
            Slots.PreviewConfiguration = {Shape + 1, 1, 0, 0};
            Confirm(ValidateMaterialScattering(Slots), "Scattering contract rejected a selectable preview shape.");
        }
        Slots.PreviewConfiguration = {6, 1, 0, 0};
        Confirm(!ValidateMaterialScattering(Slots), "Scattering contract accepted an unknown preview slot.");
        Inspection = {};
        Inspection.Exposure = 4.01f;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Out-of-range exposure accepted.");
        Inspection = {};
        Inspection.PanX = std::numeric_limits<float>::infinity();
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Infinite pan accepted.");
        Inspection = {};
        Inspection.Pitch = 2;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Unbounded orbit pitch accepted.");
        Inspection = {};
        Inspection.Shape = MaterialPreviewShape::Cube;
        Inspection.PanX = .3f;
        Inspection.PanY = -.2f;
        Inspection.Yaw = .8f;
        Inspection.Pitch = -.4f;
        const auto Camera = ConstructMaterialPreviewProjection(Inspection);
        // 📝 Perspective rig: the shape pivot must land on the pan point in NDC for every orbit/zoom.
        const auto ProjectNdc = [](const MaterialPreviewProjection &Pose, float X, float Y, float Z)
        {
            const auto &M = Pose.ClipFromSurface;
            const float W = M[3] * X + M[7] * Y + M[11] * Z + M[15];
            return std::array<float, 2>{
                (M[0] * X + M[4] * Y + M[8] * Z + M[12]) / W, (M[1] * X + M[5] * Y + M[9] * Z + M[13]) / W};
        };
        const auto Centred = ProjectNdc(Camera, 48, 0, 0);
        Confirm(std::abs(Centred[0] - Inspection.PanX) < .0001f && std::abs(Centred[1] - Inspection.PanY) < .0001f,
            "Rotating an offset inspection shape lost its camera centre.");
        // 📝 Zero-ellipse turntable yaw is incompatible with a screen-vertical object up-axis: demanding a
        // fixed front-point height forces the up-axis to lean sideways (and pitch to roll at 90° yaw). Standard
        // orbit tools trace screen ellipses instead, so yaw asserts the up-axis, pitch asserts the tip axis.
        for (float Theta : {0.f, .7f, -1.2f, 2.4f})
            for (float Pitch : {-1.2f, -.4f, 0.f, .5f, 1.2f})
            {
                MaterialPreviewConfiguration Yawed;
                Yawed.Shape = MaterialPreviewShape::Cube;
                Yawed.Yaw = Theta;
                Yawed.Pitch = Pitch;
                const auto Pose = ConstructMaterialPreviewProjection(Yawed);
                const auto &View = Pose.ViewFromSurface;
                const auto Pivot = ProjectNdc(Pose, 48, 0, 0);
                Confirm(std::abs(Pivot[0]) < .0001f && std::abs(Pivot[1]) < .0001f, "Yaw orbit lost the pivot centre.");
                Confirm(std::abs(View[4]) < .0001f, "Yaw orbit leaned the object up-axis sideways.");
            }
        float EllipseLow = std::numeric_limits<float>::max(), EllipseHigh = std::numeric_limits<float>::lowest();
        for (float Theta : {0.f, .7f, -1.2f, 2.4f})
        {
            MaterialPreviewConfiguration Yawed;
            Yawed.Shape = MaterialPreviewShape::Cube;
            Yawed.Yaw = Theta;
            Yawed.Pitch = -.4f;
            const auto Pose = ConstructMaterialPreviewProjection(Yawed);
            const auto OffPivot = ProjectNdc(Pose, 48, 0, 1);
            EllipseLow = std::min(EllipseLow, OffPivot[1]);
            EllipseHigh = std::max(EllipseHigh, OffPivot[1]);
        }
        Confirm(EllipseHigh - EllipseLow > .1f, "Pitched yaw lost its standard screen ellipse.");
        for (float Pitch : {-.9f, -.4f, 0.f, .5f, 1.2f})
        {
            MaterialPreviewConfiguration Pitched;
            Pitched.Shape = MaterialPreviewShape::Sphere;
            Pitched.Yaw = 1.5707963f;
            Pitched.Pitch = Pitch;
            const auto Pose = ConstructMaterialPreviewProjection(Pitched);
            const auto &View = Pose.ViewFromSurface;
            const auto Pivot = ProjectNdc(Pose, 16, 0, 0);
            Confirm(std::abs(Pivot[0]) < .0001f && std::abs(Pivot[1]) < .0001f, "Pitched yaw lost the pivot centre.");
            Confirm(std::abs(View[4]) < .0001f, "Pitched a yawed shape about the view axis (roll).");
        }
        for (const auto Shape : {MaterialPreviewShape::ShaderBall, MaterialPreviewShape::Sphere, MaterialPreviewShape::Plane,
                                 MaterialPreviewShape::Cube, MaterialPreviewShape::Cloth})
        {
            MaterialPreviewConfiguration Centred;
            Centred.Shape = Shape;
            Centred.Zoom = 2;
            Centred.PanX = .3f;
            Centred.PanY = -.2f;
            Centred.Yaw = .8f;
            Centred.Pitch = -.4f;
            const auto Pose = ConstructMaterialPreviewProjection(Centred);
            const float Translation = 16.f * uint32_t(Shape);
            const float Pivots[5][3]{{0, -.002f, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, -.041f, 0}};
            const auto Index = size_t(uint32_t(Shape));
            const auto Pivot = ProjectNdc(Pose, Translation + Pivots[Index][0], Pivots[Index][1], Pivots[Index][2]);
            Confirm(std::abs(Pivot[0] - Centred.PanX) < .0001f && std::abs(Pivot[1] - Centred.PanY) < .0001f,
                "Combined orbit drifted a shape centre off its pan point.");
        }
        // 📝 Alloy capture framing: every specimen rim fills 1/(4.296·tan17°) ≈ .7614 of the half-frame
        // at default zoom, matching the HTML r=1.35 ball at distance 5.8; zoom 2 doubles the magnification.
        const float Radii[5]{1.085f, .85f, 1.131f, 1.074f, .959f};
        for (const auto Shape : {MaterialPreviewShape::ShaderBall, MaterialPreviewShape::Sphere, MaterialPreviewShape::Plane,
                                 MaterialPreviewShape::Cube, MaterialPreviewShape::Cloth})
        {
            const auto Index = size_t(uint32_t(Shape));
            const float Pivots[5][3]{{0, -.002f, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, -.041f, 0}};
            MaterialPreviewConfiguration Framed;
            Framed.Shape = Shape;
            const float Translation = 16.f * uint32_t(Shape);
            const auto Wide = ProjectNdc(ConstructMaterialPreviewProjection(Framed),
                Translation + Pivots[Index][0] + Radii[Index], Pivots[Index][1], Pivots[Index][2]);
            Confirm(std::abs(Wide[0] - .7614f) < .002f && std::abs(Wide[1]) < .002f, "Alloy capture framing drifted.");
            Framed.Zoom = 2;
            const auto Tight = ProjectNdc(ConstructMaterialPreviewProjection(Framed),
                Translation + Pivots[Index][0] + Radii[Index], Pivots[Index][1], Pivots[Index][2]);
            Confirm(std::abs(Tight[0] - 2 * Wide[0]) < .002f, "Perspective zoom is not a dolly.");
        }
        // 📝 Stage level: every stand lathe ring shares one height per radius, so the world-up
        // stage can never roll while the camera orbits (the retired specimen-group pitch spread
        // rim heights by ~.34 across the stand, which read as the specimen tumbling).
        {
            const auto Specimen = ConstructMaterialPreviewSpecimen();
            std::array<float, 4096> Low{}, High{};
            Low.fill(1e30f);
            High.fill(-1e30f);
            for (const auto &Corner : Specimen.Corners)
            {
                if (Corner.Coordinates[0] != -1.f) continue;
                const float Radial = std::hypot(Corner.Position[0], Corner.Position[2]);
                if (Radial < .05f) continue; // cap centres share the axis at both disc heights
                const auto Bucket = size_t(std::lround(Radial * 2000));
                if (Bucket >= Low.size()) continue;
                Low[Bucket] = std::min(Low[Bucket], Corner.Position[1]);
                High[Bucket] = std::max(High[Bucket], Corner.Position[1]);
            }
            float Spread = 0;
            for (size_t Bucket = 0; Bucket < Low.size(); ++Bucket)
                if (High[Bucket] > -1e29f) Spread = std::max(Spread, High[Bucket] - Low[Bucket]);
            Confirm(Spread < .001f, "Pedestal lathe rings are not level; orbiting would roll the stage.");
        }
        // 📝 World-locked studio: exact identity at the default pose, rigid carry under orbit,
        // and environment up equal to world up in view space at any yaw.
        {
            const std::array<float, 3> Reference{-.4f, .7f, .6f};
            const auto Identity = ConstructMaterialPreviewLightDirection(MaterialPreviewConfiguration{}, Reference);
            Confirm(std::abs(Identity[0] + .4f) < .0001f && std::abs(Identity[1] - .7f) < .0001f &&
                        std::abs(Identity[2] - .6f) < .0001f,
                "Default-pose light direction moved.");
            MaterialPreviewConfiguration Orbited;
            Orbited.Yaw = 3.1415927f;
            const auto Carried = ConstructMaterialPreviewLightDirection(Orbited, Reference);
            Confirm(std::abs(Carried[0] - .4f) < .001f && std::abs(Carried[1] - .87309f) < .001f &&
                        std::abs(Carried[2] + .29616f) < .001f,
                "Orbited light direction is not the rigid world carry.");
            const auto Up = ConstructMaterialPreviewEnvironmentUp(MaterialPreviewConfiguration{});
            Confirm(std::abs(Up[0]) < .0001f && std::abs(Up[1] - .98185f) < .0001f && std::abs(Up[2] - .18964f) < .0001f,
                "Default environment up is not world up in view space.");
            MaterialPreviewConfiguration PitchedUp;
            PitchedUp.Yaw = 2.f;
            PitchedUp.Pitch = .5f;
            const auto CarriedUp = ConstructMaterialPreviewEnvironmentUp(PitchedUp);
            Confirm(std::abs(CarriedUp[0]) < .0001f && std::abs(CarriedUp[1] - .77074f) < .0001f &&
                        std::abs(CarriedUp[2] - .63715f) < .0001f,
                "Environment up is not yaw-invariant world up.");
        }
        Inspection.Environment = 2;
        Inspection.Exposure = 2;
        ResetMaterialPreviewView(Inspection);
        Confirm(Inspection.Shape == MaterialPreviewShape::Cube && Inspection.Environment == 2 &&
            Inspection.Exposure == 2 && Inspection.PanX == 0 && Inspection.Yaw == 0 && Inspection.Zoom == 1,
            "View reset changed authored inspection lighting/shape.");
        std::string Diagnostic;
        const auto Geometry = ConstructMaterialInspectionGeometry();
        Confirm(ValidateSurfaceGeometry(Geometry, Diagnostic), Diagnostic.c_str());
        // 📝 The draw ranges must partition the index list exactly and keep every index inside its own
        // 16-unit region, or the preview rasterizes neighbouring specimens again (reported two-shape bug).
        const auto InspectionGeometry = ConstructMaterialInspectionGeometryWithRanges();
        uint32_t RangeTotal = 0;
        for (size_t Shape = 0; Shape < InspectionGeometry.ShapeIndexRanges.size(); ++Shape)
        {
            const auto &Range = InspectionGeometry.ShapeIndexRanges[Shape];
            Confirm(Range[0] == RangeTotal, "Shape index ranges are not contiguous.");
            Confirm(Range[1] > 0, "Shape index range is empty.");
            for (uint32_t At = Range[0]; At < Range[0] + Range[1]; ++At)
            {
                const auto Corner = InspectionGeometry.Geometry.TriangleCorners[At];
                const int Region = int(std::round(InspectionGeometry.Geometry.Corners[Corner].Position[0] / 16.f));
                Confirm(Region == int(Shape), "Shape index range references a neighbouring specimen.");
            }
            RangeTotal += Range[1];
        }
        Confirm(RangeTotal == InspectionGeometry.Geometry.TriangleCorners.size(), "Shape index ranges do not cover the geometry.");
        std::array<size_t,5> Populations{};
        size_t NegativeCubeTangents = 0;
        float ClothLowZ = 1, ClothHighZ = -1, ClothLowNormalZ = 1, ClothHighNormalZ = -1;
        for (const auto &Corner : Geometry.Corners)
        {
            const int Region = int(std::round(Corner.Position[0] / 16.f));
            Confirm(Region >= 0 && Region <= 4, "Inspection geometry escaped separated regions.");
            ++Populations[size_t(Region)];
            if (Region == 3 && Corner.Tangent[3] == -1) ++NegativeCubeTangents;
            if (Region == 4)
            {
                ClothLowZ = std::min(ClothLowZ, Corner.Position[2]);
                ClothHighZ = std::max(ClothHighZ, Corner.Position[2]);
                ClothLowNormalZ = std::min(ClothLowNormalZ, Corner.Normal[2]);
                ClothHighNormalZ = std::max(ClothHighNormalZ, Corner.Normal[2]);
            }
        }
        for (auto Population : Populations) Confirm(Population > 0, "Inspection surface missing.");
        Confirm(NegativeCubeTangents == 12, "Negative cube faces lost normal-map tangent handedness.");
        Confirm(ClothHighZ - ClothLowZ > .2f, "Draped cloth swatch is flat.");
        Confirm(ClothHighNormalZ - ClothLowNormalZ > .05f, "Draped cloth folds did not perturb normals.");
        DiagnosticJournal::Initialize("InspectionValidation");
        DiagnosticJournal::Append("Acceptance", "Preview controls validation\nembedded newline");
        const auto Log = DiagnosticJournal::QueryFilename();
        Confirm(std::filesystem::is_regular_file(Log), "Persistent diagnostic file missing.");
        std::ifstream Input(Log);
        std::string FirstLine;
        std::getline(Input, FirstLine);
        Confirm(FirstLine.find("ms [Acceptance]") != std::string::npos, "Timestamp/category missing from diagnostic.");
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        IO.DisplaySize = ImVec2(1440,1000);
        IO.DeltaTime = 1.f / 60;
        unsigned char *Pixels = nullptr;
        int Width = 0, Height = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);
        IO.Fonts->SetTexID(ImTextureID(1));
        ActivationOwnership Ownership(new MaterialLibraryPanel());
        auto &Panel = *static_cast<MaterialLibraryPanel *>(Ownership.get());
        Confirm(Panel.Initialize() && Panel.Activate(), "Preview panel activation failed.");
        Confirm(Panel.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio"), "Material fixtures unavailable.");
        uint32_t Notifications = 0;
        Panel.QueryEnvironmentNames = []()
        {
            return std::vector<std::string>{"Studio softbox", "Studio strip", "Window light", "Overcast"};
        };
        Panel.QueryEnvironmentPreview = [](size_t) { return MaterialPreviewImage{}; };
        Panel.RescanEnvironments = []() {};
        Panel.ConfigurePreview = [&](const MaterialPreviewConfiguration &Configuration)
        {
            Confirm(ValidateMaterialPreviewConfiguration(Configuration), "UI emitted invalid inspection settings.");
            ++Notifications;
        };
        const auto Integrate = [&]()
        {
            ImGui::NewFrame();
            Panel.ConstructInterface();
            ImGui::Render();
        };
        for (int Index = 0; Index < 4; ++Index) Integrate();
        const auto Click = [&](size_t Index)
        {
            const auto Extent = Panel.QueryPreviewControlExtent(Index);
            Confirm(Extent.MaximumX > Extent.MinimumX && Extent.MaximumY > Extent.MinimumY, "Preview control has no clickable extent.");
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) / 2, (Extent.MinimumY + Extent.MaximumY) / 2);
            Integrate();
            IO.AddMouseButtonEvent(0,true);
            Integrate();
            IO.AddMouseButtonEvent(0,false);
            Integrate();
        };
        Click(2);
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Zoom-in pill did not respond to actual pointer input.");
        Click(0);
        Confirm(std::abs(Panel.QueryPreviewConfiguration().Zoom - 1) < .001f, "Zoom-out pill did not respond.");
        Click(2);
        Click(1);
        Confirm(Panel.QueryPreviewConfiguration().Zoom == 1, "Zoom percentage reset did not respond.");
        const auto DragCanvas = [&](int Button, float Horizontal, float Vertical, bool Shift = false)
        {
            const auto Canvas = Panel.QueryPreviewControlExtent(7);
            const ImVec2 Centre((Canvas.MinimumX + Canvas.MaximumX) / 2, (Canvas.MinimumY + Canvas.MaximumY) / 2);
            IO.AddMousePosEvent(Centre.x, Centre.y);
            IO.AddKeyEvent(ImGuiMod_Shift, Shift);
            Integrate();
            IO.AddMouseButtonEvent(Button, true);
            Integrate();
            IO.AddMousePosEvent(Centre.x + Horizontal, Centre.y + Vertical);
            Integrate();
            IO.AddMouseButtonEvent(Button, false);
            IO.AddKeyEvent(ImGuiMod_Shift, false);
            Integrate();
        };
        DragCanvas(2, 30, 20);
        Confirm(Panel.QueryPreviewConfiguration().PanX > 0 && Panel.QueryPreviewConfiguration().PanY > 0,
            "MMB pan did not respond to actual pointer drag.");
        Click(6);
        Confirm(Panel.QueryPreviewConfiguration().PanX == 0 && Panel.QueryPreviewConfiguration().PanY == 0,
            "Reset view did not centre the specimen.");
        DragCanvas(0, 20, 15, true);
        Confirm(Panel.QueryPreviewConfiguration().PanX > 0 && Panel.QueryPreviewConfiguration().Yaw == 0,
            "Shift-left drag did not pan independently of orbit.");
        Click(6);
        DragCanvas(0, 35, 20);
        Confirm(Panel.QueryPreviewConfiguration().Yaw != 0 && Panel.QueryPreviewConfiguration().Pitch != 0,
            "Left drag did not orbit the specimen.");
        const auto BeforeRelease = Panel.QueryPreviewConfiguration();
        IO.AddMousePosEvent(10,10);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration() == BeforeRelease, "Released pointer continued moving the camera.");
        Click(6);
        DragCanvas(1, 0, -30);
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Right drag did not zoom.");
        Click(6);
        const auto Canvas = Panel.QueryPreviewControlExtent(7);
        IO.AddMousePosEvent((Canvas.MinimumX + Canvas.MaximumX) / 2, (Canvas.MinimumY + Canvas.MaximumY) / 2);
        Integrate();
        IO.AddMouseWheelEvent(0,1);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Canvas wheel zoom did not respond.");
        IO.AddMouseWheelEvent(0,100);
        Integrate();
        Confirm(std::abs(Panel.QueryPreviewConfiguration().Zoom - 5) < .001f,
            "Canvas wheel zoom did not reach the supported 500% magnification.");
        Click(6);
        Confirm(Panel.QueryPreviewConfiguration().Zoom == 1, "Preview did not reset cleanly from 500% magnification.");
        // 📝 Preview keys only drive the camera while the pointer hovers the inspection canvas, so parked
        // widget/popup input cannot move it. Return the pointer there before exercising F and arrow keys.
        IO.AddMousePosEvent((Canvas.MinimumX + Canvas.MaximumX) / 2, (Canvas.MinimumY + Canvas.MaximumY) / 2);
        Integrate();
        IO.AddMouseWheelEvent(0,1);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Canvas wheel zoom did not respond before F reset.");
        IO.AddKeyEvent(ImGuiKey_F,true);
        Integrate();
        IO.AddKeyEvent(ImGuiKey_F,false);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Zoom == 1, "F did not reset the camera.");
        IO.AddKeyEvent(ImGuiKey_RightArrow,true);
        Integrate();
        IO.AddKeyEvent(ImGuiKey_RightArrow,false);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().PanX > 0, "Keyboard pan did not respond.");
        Click(6);
        const auto BeforeWidgetWheel = Panel.QueryPreviewConfiguration();
        const auto ShapeExtent = Panel.QueryPreviewControlExtent(3);
        IO.AddMousePosEvent((ShapeExtent.MinimumX + ShapeExtent.MaximumX) / 2, (ShapeExtent.MinimumY + ShapeExtent.MaximumY) / 2);
        Integrate();
        IO.AddMouseWheelEvent(0,1);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration() == BeforeWidgetWheel, "Wheel over a selector leaked into preview navigation.");
        const auto SelectPreset = [&](size_t Control, int Index)
        {
            Click(Control);
            Integrate();
            Integrate();
            auto *Context = ImGui::GetCurrentContext();
            Confirm(!Context->OpenPopupStack.empty(), "Preview selector did not open a popup.");
            auto *Popup = Context->OpenPopupStack.back().Window;
            Confirm(Popup != nullptr, "Preview selector popup has no window.");
            const float RowHeight = ControlKitTokens::DropdownRowHeight + ControlKitTokens::DropdownRowGap;
            const auto Position = Popup->DC.CursorStartPos;
            IO.AddMousePosEvent(Position.x + 20, Position.y + Index * RowHeight + ControlKitTokens::DropdownRowHeight * .5f);
            Integrate();
            IO.AddMouseButtonEvent(0,true);
            Integrate();
            IO.AddMouseButtonEvent(0,false);
            Integrate();
        };
        for (int Index = 1; Index <= 4; ++Index)
        {
            SelectPreset(3, Index);
            Confirm(int(Panel.QueryPreviewConfiguration().Shape) == Index, "Preview shape selector did not respond to pointer selection.");
        }
        // 📝 Environment card: open the popover, drag the exposure slider inside it, then pick a
        // sphere-preview tile (which must also dismiss the card).
        Click(4);
        Integrate();
        Integrate();
        Confirm(!ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Environment card did not open.");
        const auto ExposureExtent = Panel.QueryPreviewControlExtent(5);
        Confirm(ExposureExtent.Width() > 0, "Exposure slider extent not recorded inside the environment card.");
        IO.AddMousePosEvent(ExposureExtent.MinimumX + (ExposureExtent.MaximumX - ExposureExtent.MinimumX) * .85f,
            (ExposureExtent.MinimumY + ExposureExtent.MaximumY) * .5f);
        Integrate();
        IO.AddMouseButtonEvent(0,true);
        Integrate();
        IO.AddMouseButtonEvent(0,false);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Exposure != 1, "Shared exposure slider did not respond to pointer input.");
        const auto TileExtent = Panel.QueryEnvironmentPreviewExtent(2);
        Confirm(TileExtent.Width() > 0 && TileExtent.Height() > 0, "Environment preview tile extent not recorded.");
        IO.AddMousePosEvent((TileExtent.MinimumX + TileExtent.MaximumX) * .5f, (TileExtent.MinimumY + TileExtent.MaximumY) * .5f);
        Integrate();
        IO.AddMouseButtonEvent(0,true);
        Integrate();
        IO.AddMouseButtonEvent(0,false);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Environment == 2, "Environment preview tile did not select its archive.");
        Confirm(ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Environment card did not close after tile selection.");
        const auto Fullscreen = Panel.QueryFullscreenPreviewExtent();
        Confirm(Fullscreen.Width() > 0 && Fullscreen.Height() > 0, "Fullscreen preview action has no pointer extent.");
        IO.AddMousePosEvent((Fullscreen.MinimumX + Fullscreen.MaximumX) * .5f, (Fullscreen.MinimumY + Fullscreen.MaximumY) * .5f);
        Integrate(); IO.AddMouseButtonEvent(0,true); Integrate(); IO.AddMouseButtonEvent(0,false); Integrate();
        Confirm(Panel.QueryFullscreenPreview() && Panel.QueryPreviewConfiguration().Zoom == 1 &&
            Panel.QueryPreviewConfiguration().PanX == 0 && Panel.QueryPreviewConfiguration().PanY == 0,
            "Fullscreen preview did not open with the specimen fitted and centred.");
        const ImVec2 FullscreenCentre(IO.DisplaySize.x * .5f, IO.DisplaySize.y * .5f);
        IO.AddMousePosEvent(FullscreenCentre.x, FullscreenCentre.y);
        Integrate(); IO.AddMouseButtonEvent(0,true); Integrate();
        IO.AddMousePosEvent(FullscreenCentre.x + 24, FullscreenCentre.y + 18); Integrate();
        IO.AddMouseButtonEvent(0,false); Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Yaw != 0 && Panel.QueryPreviewConfiguration().Pitch != 0,
            "Fullscreen canvas drag did not orbit the specimen.");
        IO.AddKeyEvent(ImGuiKey_Escape,true); Integrate(); IO.AddKeyEvent(ImGuiKey_Escape,false); Integrate();
        Confirm(!Panel.QueryFullscreenPreview(), "Escape did not close fullscreen preview.");
        Confirm(Notifications >= 12, "Preview settings callbacks were not delivered.");
        Ownership.reset();
        ImGui::DestroyContext();
        std::ofstream Report(Arguments[1]);
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"actual_zoom_pointer_events\":true,\"pan_orbit_wheel_keyboard_release_and_widget_isolation\":true,\"actual_shape_environment_exposure_pointer_events\":true}\n";
        Confirm(bool(Report), "Could not write preview acceptance report.");
        std::cout << "Material inspection checks passed: " << Checks << '\n';
        return 0;
    }
    catch (const std::exception &Failure)
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
