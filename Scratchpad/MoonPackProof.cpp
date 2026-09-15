// Does the sequence pack the same moons for the kernel that ApplyTo hands the raster?
//
// PackMoonRecord feeds binding 22; ApplyTo feeds the CPU raster. Both must resolve the same roster — the linked
// Luna from the solved frame, placed moons from azimuth/elevation, a hidden entity as a moonless sky — or the
// GI-on and GI-off nights show different moons. This packs records from a live sequence and checks each behaviour,
// including the sheet round-trip: four slot groups share one sheet, and Find reads back BY LABEL, so an
// unprefixed label would land every slot's edit on slot one.
#include "Projects/Project-Zero/Source/CelestialSequence.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace Frontier;
using namespace Frontier::ProjectZero;

namespace {
int Failures=0;
void Expect(bool Ok,const char* What){ if(!Ok)++Failures;
    std::printf("  %-66s %s\n",What,Ok?"PASS":"FAIL"); }

bool Nearly(float A, float B)
{
    const float Scale = 1.0f + std::fabs(A) + std::fabs(B);
    return std::fabs(A - B) <= 1e-5f * Scale;
}
bool Nearly3(const float A[3], const float B[3]) { return Nearly(A[0],B[0]) && Nearly(A[1],B[1]) && Nearly(A[2],B[2]); }

// Independent horizon math, so the pack is checked against the convention, not against itself.
void AzEl(float ElDeg, float AzDeg, float Out[3])
{
    const float E = ElDeg * 3.14159265358979323846f / 180.0f;
    const float A = AzDeg * 3.14159265358979323846f / 180.0f;
    Out[0] = std::cos(E) * std::sin(A); Out[1] = std::cos(E) * std::cos(A); Out[2] = std::sin(E);
}
}

static_assert(sizeof(MoonConstantRecord) == 288u, "the packed record is the shader's 288-byte block");
static_assert(offsetof(MoonConstantRecord, Slots) == 16u, "the slot row follows the control word");

int main(){
    std::printf("\nCelestialSequence::PackMoonRecord — the kernel is packed the raster's moons\n");

    CelestialSequence Sky;
    Sky.Prepare();

    // The atlas, registered but never decoded: the pack needs slots, not pixels, so this proof runs with no
    //    image files and no STB decode — the render proof covers the pixels.
    TextureIndex Textures;
    uint32_t Slots[kMoonAtlasCount];
    for (uint32_t M = 0u; M < kMoonAtlasCount; ++M)
    {
        char Path[128];
        std::snprintf(Path, sizeof(Path), "%s%s", kMoonTextureDirectory, kMoonAtlas[M].File);
        Slots[M] = Textures.RegisterPath(Path, /*Linear=*/false);
    }
    Sky.AssignMoonAtlas(Slots, Textures);

    // ① The default roster is one moon, Luna, and she follows the solved frame — before any tick has run.
    {
        const MoonConstantRecord R = Sky.PackMoonRecord();
        Expect(R.Control[0] == 1u, "the default roster packs exactly one moon");
        Expect(Nearly3(R.Direction[0], Sky.Frame().Moon.Direction), "a linked Luna reads the solved lunar direction");
        Expect(Nearly(R.Params[0][2], MoonPhaseToReference(Sky.Frame().MoonPhase)), "a linked Luna reads the solved phase, converted");
        Expect(Nearly(R.Params[0][0], kMoonAtlas[0].SizeDegrees * 3.14159265358979323846f / 360.0f), "the diameter packs as a radius in radians");
        Expect(R.Slots[0] == Slots[0], "the Luna entry carries the Luna bindless slot");
        Expect(Nearly(R.Surface[0][1], 6.7f * 3.14159265358979323846f / 180.0f), "the skin (tilt) comes from the atlas preset");
        Expect(R.Surface[0][2] == 0.0f && R.Surface[0][3] == 1.0f, "the skin (haze, gamma) comes from the atlas preset");
        Expect(R.Params[0][1] == 1.6f && R.Params[0][3] == 0.8f, "bright and glow keep the panel's defaults");
    }

    // ② After a tick the ephemeris has moved and the pack tracks it.
    {
        const float Camera[3] = { 0.0f, 0.0f, 2.0f };
        Sky.Observation.LocalHours += 3.0f;
        Sky.Tick(0.016f, Camera, 0.0f);
        const MoonConstantRecord R = Sky.PackMoonRecord();
        Expect(Nearly3(R.Direction[0], Sky.Frame().Moon.Direction), "after a tick the pack still reads the solved frame");
    }

    // ③ A hidden Moons entity is a moonless sky, not a black one: the count goes to zero and the stale bytes
    //    stand unread — while re-showing restores the roster, exactly as ApplyTo re-lends the list.
    Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = false;
    {
        const MoonConstantRecord R = Sky.PackMoonRecord();
        Expect(R.Control[0] == 0u, "a hidden Moons entity packs a zero count");
    }
    Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = true;
    {
        const MoonConstantRecord R = Sky.PackMoonRecord();
        Expect(R.Control[0] == 1u, "re-showing restores the roster");
    }

    // ④ The master switch and the missing atlas both pack zero — a caller without textures gets no moons, not
    //    slot 0's material.
    {
        Sky.Enabled = false;
        const MoonConstantRecord Off = Sky.PackMoonRecord();
        Expect(Off.Control[0] == 0u, "disabling the sequence packs no moons");
        Sky.Enabled = true;
        CelestialSequence Bare;
        Bare.Prepare();
        const MoonConstantRecord NoAtlas = Bare.PackMoonRecord();
        Expect(NoAtlas.Control[0] == 0u, "no atlas assigned packs no moons");
    }

    // ⑤ A placed moon resolves from azimuth/elevation against the independent horizon math, and a preset past
    //    the atlas falls back to Luna's skin rather than reading out of bounds.
    Sky.MoonSlots[1].Visible = true;
    Sky.MoonSlots[1].FollowSky = false;
    Sky.MoonSlots[1].Preset = 1u;
    Sky.MoonSlots[1].Azimuth = 123.0f;
    Sky.MoonSlots[1].Elevation = 17.0f;
    Sky.MoonSlots[1].Phase = 0.5f;
    {
        const MoonConstantRecord R = Sky.PackMoonRecord();
        Expect(R.Control[0] == 2u, "an unhidden slot joins the count");
        float Want[3]; AzEl(17.0f, 123.0f, Want);
        Expect(Nearly3(R.Direction[1], Want), "a placed moon resolves from azimuth/elevation");
        Expect(Nearly(R.Params[1][2], MoonPhaseToReference(0.5f)), "a placed phase converts like a solved one");
        Expect(R.Slots[1] == Slots[1], "the ember entry carries the ember bindless slot");
        Expect(Nearly(R.Surface[1][1], 25.0f * 3.14159265358979323846f / 180.0f), "switching bodies re-skins the tilt");
    }
    Sky.MoonSlots[1].Preset = 99u;
    {
        const MoonConstantRecord R = Sky.PackMoonRecord();
        Expect(Nearly(R.Surface[1][1], 6.7f * 3.14159265358979323846f / 180.0f), "a preset past the atlas falls back to Luna");
    }
    Sky.MoonSlots[1].Preset = 1u;

    // ⑥ The sheet round-trip: every slot's edits must land on that slot, which is what the M1..M4 label prefix
    //    is for — and a preset switch re-skins the size while the slot's own bright/glow/phase survive.
    {
        EditorSheet Sheet{};
        Sky.BuildSheet(CelestialEntity::Moons, Sheet);
        Expect(Sheet.GroupCount == 5u, "the moons sheet carries the solved group plus four slots");
        // Rewrite two slots' azimuths through the labels the sheet actually wrote, then read them back.
        uint32_t Found1 = 0u, Found2 = 0u;
        for (uint32_t G = 0; G < Sheet.GroupCount; ++G)
            for (uint32_t P = 0; P < Sheet.Groups[G].PropertyCount; ++P)
            {
                EditorProperty& Prop = Sheet.Groups[G].Properties[P];
                if (std::strcmp(Prop.Label, "M1 Azimuth") == 0) { Prop.Figure = 10.0f; ++Found1; }
                if (std::strcmp(Prop.Label, "M2 Azimuth") == 0) { Prop.Figure = 20.0f; ++Found2; }
                if (std::strcmp(Prop.Label, "M2 Preset") == 0)  { Prop.Picked = 4u; }
            }
        Expect(Found1 == 1u && Found2 == 1u, "each slot's labels are unique on the sheet");
        Sky.ApplySheet(CelestialEntity::Moons, Sheet);
        Expect(Sky.MoonSlots[0].Azimuth == 10.0f && Sky.MoonSlots[1].Azimuth == 20.0f,
               "slot edits land on their own slot, not slot one");
        Expect(Sky.MoonSlots[1].Preset == 4u, "the preset switch applies");
        Expect(Sky.MoonSlots[1].Size == kMoonAtlas[4].SizeDegrees, "a preset switch re-skins the size");
        Expect(Sky.MoonSlots[1].Bright == 1.6f, "bright survives a preset switch");
    }
    Sky.MoonSlots[1].Visible = false;

    // ⑦ The raster is lent the same moons the kernel is packed: ApplyTo's list and the record agree field by
    //    field, and both go null/zero together when the entity hides.
    {
        VisibilityRaster Raster;
        CelestialBudget Budget{};
        Sky.ApplyTo(Raster, Budget);
        const VisibilityRaster::CelestialSettings& S = Raster.QueryCelestial();
        const MoonConstantRecord R = Sky.PackMoonRecord();
        Expect(S.Moons != nullptr && S.Moons->Count == R.Control[0], "the raster is lent exactly the packed count");
        bool Same = S.Moons != nullptr;
        for (uint32_t I = 0u; Same && I < R.Control[0]; ++I)
        {
            const MoonDrawEntry& E = S.Moons->Entries[I];
            Same = Same && Nearly3(E.Direction, R.Direction[I]) && Nearly3(E.Tint, R.Tint[I])
                && Nearly(E.AngularRadius, R.Params[I][0]) && Nearly(E.Brightness, R.Params[I][1])
                && Nearly(E.Phase, R.Params[I][2]) && Nearly(E.Glow, R.Params[I][3])
                && Nearly(E.Spin, R.Surface[I][0]) && Nearly(E.Tilt, R.Surface[I][1])
                && Nearly(E.Haze, R.Surface[I][2]) && Nearly(E.Gamma, R.Surface[I][3])
                && E.TextureSlot == R.Slots[I];
        }
        Expect(Same, "every lent field equals its packed twin");
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = false;
        Sky.ApplyTo(Raster, Budget);
        Expect(Raster.QueryCelestial().Moons == nullptr, "hiding the entity unlends the list");
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = true;
    }

    // ⑧ The phase conversion is self-inverse: engine 0.5 (full) is reference 0.0 (full) and back.
    {
        bool Round = true;
        for (float P = 0.0f; P < 1.0f; P += 0.125f)
            Round = Round && Nearly(MoonPhaseToReference(MoonPhaseToReference(P)), P);
        Expect(Round, "the phase conversion round-trips");
        Expect(Nearly(MoonPhaseToReference(0.5f), 0.0f), "engine full (0.5) is reference full (0.0)");
    }

    std::printf("\n");
    for(int I=0;I<108;++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures==0 ? "  the kernel is packed the raster's moons" : "  THE PACK AND THE RASTER DISAGREE");
    return Failures==0?0:1;
}
