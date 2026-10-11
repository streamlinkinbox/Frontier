//==============================================================================================================================================
//                                                         NATIVEFORCECARD.CPP
//==============================================================================================================================================
// 📦 Executed proof: the native force field inspector, recorded from real ImGui draw commands, against the
//    browser panel it was ported from — Experimental/ParticleEditor/js/app.js renderForcesInspector over
//    js/forcefields.js — and against the taxonomy it is drawn from,
//    Engine/VolumetricDynamics/ForceFieldSet.h.
//
//    Four kinds of claim, and only the first could be seen in a screenshot:
//
//    ① THE LAYOUT. The stack is a CSS block flow, so the heights are pinned, and a force slider row must
//       be the same 11 px label, 8 px margin and 30 px pill a gas slider row is. It is, because it is the
//       same painter; this proof is what stops that quietly becoming untrue.
//    ② THE GROUPING IS DERIVED. Each card lands under a heading chosen by FactsOf(Kind).Give, not by a
//       hand-written list, so a kind added to the taxonomy cannot go missing from the panel. The proof
//       walks every kind in the enum and demands a home for each.
//    ③ EVERYWHERE HAS NO CENTRE AND NO RADIUS. 🔴 Not greyed out — absent. A radius control on gravity is
//       a lie with a handle on it, and the row planner is what makes that structural.
//    ④ A KIND WITH NO SHADER PATH SAYS SO. Drag and Current are authored, listed, and carry a plain line
//       admitting they move nothing yet.

#include "ForceFieldCardSurface.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::ForceCards;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition)
    {
        std::fprintf(stderr, "  FAIL  %s\n", Claim);
        throw std::runtime_error(Claim);
    }
    std::printf("  PASS  %s\n", Claim);
}
void Banner(const char* Title) { std::printf("\n%s\n", Title); }
bool Near(float A, float B, float Tolerance = 0.01f) { return std::fabs(A - B) <= Tolerance; }

bool Plans(const ForceRowPlan& Plan, ForceRow Row)
{
    for (uint32_t At = 0u; At < Plan.Count; ++At) if (Plan.Rows[At] == Row) return true;
    return false;
}

ForceField Make(ForceFieldKind Kind, const char* Name, float Strength)
{
    ForceField Field;
    Field.Kind     = Kind;
    Field.Reaches  = FactsOf(Kind).Reaches;
    Field.Strength = Strength;
    std::snprintf(Field.Name, sizeof(Field.Name), "%s", Name);
    return Field;
}
}   // namespace

int main()
{
    std::printf("NativeForceCard - the ported force field panel\n");
    std::filesystem::create_directories("Exhibits/Gallery/ForceCardNative");

    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig LightConfig;
    std::snprintf(LightConfig.Name, sizeof(LightConfig.Name), "Sun reference / light");
    ImFont* Light = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Light.ttf", 14,
                                                 &LightConfig);
    ImFontConfig RegularConfig;
    std::snprintf(RegularConfig.Name, sizeof(RegularConfig.Name), "Sun reference / regular");
    ImFont* Regular = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14,
                                                   &RegularConfig);
    ImGui::StyleColorsDark();

    constexpr int   Margin   = 20;
    constexpr float CardWide = 300.0f;                       // [px] the shipped inspector column
    const int Width = int(CardWide) + Margin * 2;
    int Height = 1400;
    ForceCardSubject Subject;
    ForceHitRegions  Where;

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Light, 14.0f);
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, Kit::PageFill);
        PaintForceCard(Draw, Light, Regular, { float(Margin), float(Margin) }, CardWide, Subject, &Where);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        Height = int(std::ceil(ResolveStack(Light, CardWide, Subject).Total)) + Margin * 2;
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        constexpr int Over = 3;                              // 3x supersample, as the other card proofs use
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
        const std::string Path = "Exhibits/Gallery/ForceCardNative/" + std::string(Name) + ".png";
        Check(PngWriteCounterpart::WritePng(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0,
              (std::string("capture written: ") + Name).c_str());
    };

    //---------------------------------------------------------------------------------------------------------
    // ② The grouping is derived from the taxonomy, not hand-written here.
    //---------------------------------------------------------------------------------------------------------
    Banner("Every kind in the taxonomy has a heading");
    unsigned Flow = 0u, Accelerate = 0u, Damp = 0u;
    for (uint32_t At = 0u; At < uint32_t(ForceFieldKind::Count); ++At)
    {
        const ForceFieldKindFacts& Facts = FactsOf(ForceFieldKind(At));
        switch (Facts.Give)
        {
            case ForceContribution::Flow:       ++Flow;       break;
            case ForceContribution::Accelerate: ++Accelerate; break;
            default:                            ++Damp;       break;
        }
        const char* Caption = ContributionCaption(Facts.Give);
        Check(Caption != nullptr && Caption[0] != '\0',
              (std::string("kind has a heading: ") + Facts.Name).c_str());
        Check(std::strlen(ContributionNote(Facts.Give)) > 80,
              (std::string("its heading explains itself: ") + Facts.Name).c_str());
    }
    Check(Flow == 5u, "five kinds contribute flow: prevailing, gust, tornado, outflow, current");
    Check(Accelerate == 6u, "six kinds contribute acceleration: attract, repel, lift, magnetic, gravity, orbit");
    Check(Damp == 1u, "one kind damps: the drag volume");
    Check(Flow + Accelerate + Damp == uint32_t(ForceFieldKind::Count), "and that is all twelve of them");

    Banner("The headings carry their units, because the quantities differ");
    Check(std::strstr(ContributionCaption(ForceContribution::Flow), "m/s") != nullptr, "flow is m/s");
    Check(std::strstr(ContributionCaption(ForceContribution::Accelerate), "m/s2") != nullptr,
          "acceleration is m/s2");
    Check(std::strstr(ContributionCaption(ForceContribution::Damp), "1/s") != nullptr, "damping is 1/s");
    Check(std::strstr(ContributionNote(ForceContribution::Flow), "coupling") != nullptr,
          "flow says the receiver's coupling decides how much it moves");
    Check(std::strstr(ContributionNote(ForceContribution::Accelerate), "Coupling has no say") != nullptr,
          "acceleration says coupling has no say, which is why gravity is here");
    Check(std::strstr(ContributionNote(ForceContribution::Damp), "never start") != nullptr,
          "damping says it can never start something moving");

    //---------------------------------------------------------------------------------------------------------
    // ③ Everywhere draws no centre and no radius.
    //---------------------------------------------------------------------------------------------------------
    Banner("Reaching everywhere means no centre and no radius");
    {
        const ForceField Gravity = Make(ForceFieldKind::Gravity, "Gravity", 9.81f);
        Check(Gravity.Reaches == ForceReach::Everywhere, "gravity reaches everywhere by its kind's own facts");
        const ForceRowPlan Plan = PlanRows(Gravity);
        Check(Plan.Everywhere, "so the planner marks the card everywhere");
        Check(!Plans(Plan, ForceRow::Centre), "no centre row is planned");
        Check(!Plans(Plan, ForceRow::Radius), "no radius row is planned");
        Check(!Plans(Plan, ForceRow::Falloff), "and no falloff row, which would have nothing to fall off over");
        Check(Plans(Plan, ForceRow::Strength), "strength remains, because it is the one thing gravity has");
        Check(std::strstr(ReachNote(), "not a very large sphere") != nullptr,
              "and the card says in words that this is not a very large sphere");

        ForceField Gust = Make(ForceFieldKind::Gust, "Gust front", 7.0f);
        const ForceRowPlan Bounded = PlanRows(Gust);
        Check(!Bounded.Everywhere, "a gust front is bounded");
        Check(Plans(Bounded, ForceRow::Centre), "so it does get a centre");
        Check(Plans(Bounded, ForceRow::Radius), "and a radius");
        Check(Plans(Bounded, ForceRow::Falloff), "and a falloff to spend it over");
        Check(Plans(Bounded, ForceRow::BandSpeed), "a gust alone carries the pulse rate row");
        Check(!Plans(PlanRows(Make(ForceFieldKind::Tornado, "Tornado", 9.0f)), ForceRow::BandSpeed),
              "a tornado does not");

        // 🔴 The same field, switched to everywhere by hand, loses the rows. The planner reads the field,
        //    not the kind, so an authored override behaves the same way the default does.
        Gust.Reaches = ForceReach::Everywhere;
        Check(!Plans(PlanRows(Gust), ForceRow::Radius),
              "a bounded kind set to everywhere loses its radius too - the planner reads the field");
    }

    Banner("Only the attractor asks about swirl and swallowing");
    Check(Plans(PlanRows(Make(ForceFieldKind::Attract, "Attractor", 6.0f)), ForceRow::Swallow),
          "the attractor has a swallow radius, because it is the kind that can consume a receiver");
    Check(!Plans(PlanRows(Make(ForceFieldKind::Repel, "Repulsor", 6.0f)), ForceRow::Swallow),
          "a repulsor cannot swallow anything and is not asked");
    Check(Plans(PlanRows(Make(ForceFieldKind::Prevailing, "Prevailing wind", 5.0f)), ForceRow::Bearing),
          "flow kinds are authored by bearing");
    Check(!Plans(PlanRows(Make(ForceFieldKind::Gravity, "Gravity", 9.81f)), ForceRow::Bearing),
          "acceleration kinds are not - a bearing on gravity would be a second way to say down");

    //---------------------------------------------------------------------------------------------------------
    // ④ No shader path is stated, not hidden.
    //---------------------------------------------------------------------------------------------------------
    Banner("A kind with no GPU path admits it");
    for (uint32_t At = 0u; At < uint32_t(ForceFieldKind::Count); ++At)
    {
        const ForceFieldKind Kind = ForceFieldKind(At);
        const ForceRowPlan Plan = PlanRows(Make(Kind, FactsOf(Kind).Name, 1.0f));
        Check(Plan.Unpackable == !ForcePackable(Kind),
              (std::string("the warning follows ForcePackable: ") + FactsOf(Kind).Name).c_str());
    }
    Check(PlanRows(Make(ForceFieldKind::Drag, "Drag volume", 1.0f)).Unpackable,
          "the drag volume is marked unpackable");
    Check(PlanRows(Make(ForceFieldKind::Current, "Current", 1.0f)).Unpackable, "so is the current");
    Check(!PlanRows(Make(ForceFieldKind::Gravity, "Gravity", 9.81f)).Unpackable, "gravity is not");
    Check(std::strstr(UnpackableNote(), "does not move anything") != nullptr,
          "and the warning says plainly that it moves nothing");

    //---------------------------------------------------------------------------------------------------------
    // The ranges are the browser's. Tools/Build/ForceFieldParity.py holds both ends to this; here the
    //    native half is pinned so a drift shows up in two places rather than one.
    //---------------------------------------------------------------------------------------------------------
    Banner("The ranges are the browser panel's ranges");
    {
        const ForceField Gust = Make(ForceFieldKind::Gust, "Gust front", 7.0f);
        Check(Near(ReadRow(Gust, ForceRow::Strength).Minimum, -20.0f), "strength floor is -20");
        Check(Near(ReadRow(Gust, ForceRow::Strength).Maximum,  20.0f), "strength ceiling is 20");
        Check(std::strcmp(ReadRow(Gust, ForceRow::Strength).Unit, "m/s") == 0,
              "and a flow field's strength is labelled m/s");
        Check(std::strcmp(ReadRow(Make(ForceFieldKind::Gravity, "Gravity", 9.81f),
                                  ForceRow::Strength).Unit, "m/s2") == 0,
              "an acceleration field's is m/s2 - the unit comes from the kind, not the row");
        Check(std::strcmp(ReadRow(Make(ForceFieldKind::Drag, "Drag volume", 1.0f),
                                  ForceRow::Strength).Unit, "1/s") == 0, "and a damping field's is 1/s");
        Check(Near(ReadRow(Gust, ForceRow::Radius).Minimum, 0.5f), "radius floor is 0.5 m");
        Check(Near(ReadRow(Gust, ForceRow::Radius).Maximum, 30.0f), "radius ceiling is 30 m");
        Check(Near(ReadRow(Gust, ForceRow::Bearing).Maximum, 360.0f), "bearing spans a full turn");
        Check(ReadRow(Gust, ForceRow::Bearing).Decimals == 0u, "to the degree, with no decimals");
        Check(Near(ReadRow(Gust, ForceRow::Lasts).Maximum, 120.0f), "a lifetime runs to two minutes");
        Check(std::strstr(RowLabel(ForceRow::Lasts), "0 = forever") != nullptr,
              "and the label says what zero means, rather than leaving it to be discovered");
        Check(std::strstr(RowLabel(ForceRow::Repeats), "0 = once") != nullptr,
              "as does the repeat row's");
    }

    //---------------------------------------------------------------------------------------------------------
    // ① The layout. Shared chrome means shared arithmetic.
    //---------------------------------------------------------------------------------------------------------
    Banner("A force row is the editor's row, to the pixel");
    Check(Near(RowHeight(ForceRow::Strength), Kit::LabelSize + Kit::LabelFoot + Kit::PillTall + Kit::FieldGap),
          "a slider row is label, margin, pill and the field gap");
    Check(Near(RowHeight(ForceRow::Enabled), Kit::SwitchTall + Kit::FieldGap),
          "a switch row is the 24 px toggle and the field gap, with no label line above it");
    Check(Near(RowHeight(ForceRow::Falloff), Kit::LabelSize + Kit::LabelFoot + Kit::SelectTall + Kit::FieldGap),
          "a select row carries the 34 px control");
    Check(Near(RowHeight(ForceRow::Remove), Kit::ActionTall + Kit::FieldGap),
          "and the remove button is the editor's 33 px action");
    Check(Near(AdderTall, Kit::ActionTall), "the add buttons are that same action button");

    Banner("A card is its rows plus the card's own padding");
    {
        const ForceField Gust = Make(ForceFieldKind::Gust, "Gust front", 7.0f);
        const ForceRowPlan Plan = PlanRows(Gust);
        float Rows = 0.0f;
        for (uint32_t At = 0u; At < Plan.Count; ++At) Rows += RowHeight(Plan.Rows[At]);
        const float Expected = Kit::CardPadTop + Kit::TitleSize + Kit::TitleFoot + Rows + Kit::CardPadFoot;
        Check(Near(CardHeight(Light, CardWide, Gust), Expected),
              "a bounded card with no notes is exactly its rows and its padding");

        const ForceField Gravity = Make(ForceFieldKind::Gravity, "Gravity", 9.81f);
        const float Grew = CardHeight(Light, CardWide, Gravity);
        const ForceRowPlan Pulled = PlanRows(Gravity);
        float Fewer = 0.0f;
        for (uint32_t At = 0u; At < Pulled.Count; ++At) Fewer += RowHeight(Pulled.Rows[At]);
        const float Inner = CardWide - Kit::CardPadX * 2.0f;
        Check(Near(Grew, Kit::CardPadTop + Kit::TitleSize + Kit::TitleFoot + Fewer
                         + Kit::NoteHeight(Light, Inner, ReachNote()) + Kit::FieldGap + Kit::CardPadFoot),
              "and an everywhere card pays for the reach note in place of the rows it dropped");
    }

    //---------------------------------------------------------------------------------------------------------
    // ② The cost card, read through ForceBakeable so the panel cannot disagree with the engine.
    //---------------------------------------------------------------------------------------------------------
    Banner("The cost card counts what the engine counts");
    {
        ForceCardSubject Scene;
        Scene.Fields[Scene.Count++] = Make(ForceFieldKind::Prevailing, "Prevailing wind", 5.0f);
        Scene.Fields[Scene.Count++] = Make(ForceFieldKind::Gust, "Gust front", 7.0f);
        Scene.Fields[Scene.Count++] = Make(ForceFieldKind::Gravity, "Gravity", 9.81f);
        Scene.Fields[Scene.Count++] = Make(ForceFieldKind::Attract, "Attractor", 6.0f);

        ForceCost Cost = ResolveCost(Scene);
        Check(Cost.Baked == 2u, "two flow fields sum into the one lattice");
        Check(Cost.Live == 2u, "the two acceleration fields are evaluated per receiver");

        // 🔴 Adding a third wind does not add a sample. Adding a third attractor does. That is the whole
        //    reason this card exists, and it is why flow and acceleration are not one list.
        Scene.Fields[Scene.Count++] = Make(ForceFieldKind::Tornado, "Tornado", 9.0f);
        Check(ResolveCost(Scene).Live == 2u, "a third wind costs no extra per-receiver work");
        Scene.Fields[Scene.Count++] = Make(ForceFieldKind::Repel, "Repulsor", 4.0f);
        Check(ResolveCost(Scene).Live == 3u, "a second repulsor-class field does");

        // A flow field that selects its receivers cannot be summed with the rest of them.
        ForceField Choosy = Make(ForceFieldKind::Gust, "Gust, debris only", 7.0f);
        Choosy.Acts = 0x2u;
        Check(!ForceBakeable(Choosy), "a selective flow field is not bakeable");
        Scene.Fields[Scene.Count++] = Choosy;
        Check(ResolveCost(Scene).Live == 4u, "so the cost card moves it to the per-receiver side");
        Check(std::strstr(CostNote(), "one velocity texture") != nullptr,
              "and the note explains why the two sides differ");
    }

    Banner("The header disowns what it does not own");
    Check(std::strstr(HeaderNote(), "not listed here") != nullptr,
          "per-system attractors are declared absent rather than silently missing");

    //---------------------------------------------------------------------------------------------------------
    // The painted panel, and where it put things.
    //---------------------------------------------------------------------------------------------------------
    Banner("The painted panel");
    Subject = ForceCardSubject{};
    Capture("Empty");
    {
        const ForceStackLayout Layout = ResolveStack(Light, CardWide, Subject);
        Check(Near(Layout.Groups, 0.0f),
              "an empty scene paints no headings at all - three lonely sentences would be worse than nothing");
        Check(Layout.Add > 0.0f && Layout.Cost > 0.0f, "but the header, the add grid and the cost card stay");
    }

    Subject.Fields[Subject.Count++] = Make(ForceFieldKind::Prevailing, "Prevailing wind", 5.0f);
    Subject.Fields[Subject.Count++] = Make(ForceFieldKind::Gust, "Southerly gust", 7.0f);
    Capture("FlowOnly");
    {
        const ForceStackLayout OneGroup = ResolveStack(Light, CardWide, Subject);
        Subject.Fields[Subject.Count++] = Make(ForceFieldKind::Gravity, "Gravity", 9.81f);
        const ForceStackLayout TwoGroups = ResolveStack(Light, CardWide, Subject);
        Check(TwoGroups.Groups > OneGroup.Groups, "a second heading appears the moment a kind needs it");
    }
    Subject.Fields[Subject.Count++] = Make(ForceFieldKind::Attract, "Whirlpool", 6.0f);
    Capture("FlowAndAcceleration");

    Subject.Fields[Subject.Count++] = Make(ForceFieldKind::Drag, "Water volume", 1.4f);
    Capture("WithDamping");

    Banner("Where the panel put its controls");
    for (int Frame = 0; Frame < 3; ++Frame) Tick();
    Check(Where.Add[uint32_t(ForceFieldKind::Gravity)].z > 0.0f, "every kind has an add button with a region");
    for (uint32_t At = 0u; At < uint32_t(ForceFieldKind::Count); ++At)
        Check(Where.Add[At].z > 0.0f,
              (std::string("addable: ") + FactsOf(ForceFieldKind(At)).Name).c_str());
    for (uint32_t At = 0u; At < Subject.Count; ++At)
    {
        Check(Where.Enabled[At].z > 0.0f && Near(Where.Enabled[At].z, Kit::SwitchWide),
              (std::string("the toggle is reachable on: ") + Subject.Fields[At].Name).c_str());
        Check(Where.Remove[At].z > 0.0f,
              (std::string("so is remove on: ") + Subject.Fields[At].Name).c_str());
        Check(Where.Enabled[At].y < Where.Remove[At].y,
              (std::string("and remove sits below the toggle on: ") + Subject.Fields[At].Name).c_str());
    }
    // 🔴 Damping is painted last, so the drag volume's card must lie below the flow cards'. If the group
    //    order ever becomes incidental, this is what notices.
    Check(Where.Remove[Subject.Count - 1u].y > Where.Remove[0].y,
          "the damping group is painted after the flow group, in the fixed order");

    Banner("Strength is draggable on every card");
    for (uint32_t At = 0u; At < Subject.Count; ++At)
        Check(Where.Strength[At].z > 0.0f && Near(Where.Strength[At].w, Kit::PillTall),
              (std::string("strength track: ") + Subject.Fields[At].Name).c_str());

    std::printf("\nNativeForceCard: %u checks passed\n", Checks);
    return 0;
}
