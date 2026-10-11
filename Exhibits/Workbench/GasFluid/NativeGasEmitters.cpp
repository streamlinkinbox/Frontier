//==============================================================================================================================================
//                                                          NATIVEGASEMITTERS.CPP
//==============================================================================================================================================
// 📦 Executed proof for the emitter component and the two gameplay hooks: routing, one-shot accounting, determinism, and smoke that goes where the tyre went.
//
// Step 10 of the port plan is the step where fluids stop being an editor feature. Five claims carry it, and
//    each is here because the defect it catches does not look like its cause:
//
//    ① A ONE-SHOT FIRES EXACTLY AS OFTEN AS IT WAS ASKED TO. Nine fracture pieces in one frame is ordinary.
//       Eight puffs, or ninety, are both wrong, and both read as a fracture bug rather than an emitter one.
//    ② AN EMITTER INSIDE TWO DOMAINS PICKS THE NEARER. Nested domains are how a room inside a street is
//       authored; picking the street would put the fire's smoke in a 24 m lattice at 32 cubed and look blurry.
//    ③ AN EMITTER INSIDE NO DOMAIN STILL RUNS. The alternative is an effect that silently does nothing in
//       exactly the case nobody authored for, which is every case gameplay produces.
//    ④ A BURNOUT LAYS ITS CLOUD DOWN THE ROAD. A disc source that spent its velocity upward would be a
//       chimney bolted to a wheel, and it is the single most recognisable tell of a borrowed effect.
//    ⑤ RESOLVING THE SAME TICK TWICE IS BIT-IDENTICAL. The solver's determinism contract is worth nothing if
//       the layer that feeds it disagrees with itself.

#include "CoarseGasField.h"
#include "GasEmitterComponent.h"
#include "GasGameplayEmission.h"
#include "GasVehicleIntake.h"
#include "GasPresetLibrary.h"
#include "GasQualityAllowance.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace Frontier;

namespace
{

unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Claim);
    if (!Condition)
    {
        throw std::runtime_error(Claim);
    }
}

void Banner(const char* Title)
{
    std::printf("\n%s\n", Title);
}

GasDomainExtent Cube(uint32_t Identity, float X, float Y, float Z, float Span)
{
    GasDomainExtent Extent;
    Extent.Identity  = Identity;
    Extent.Origin[0] = X;
    Extent.Origin[1] = Y;
    Extent.Origin[2] = Z;
    Extent.Span      = Span;
    return Extent;
}

float SmokeCentroidAlong(const CoarseGasField& Field, uint32_t Axis)
{
    float Weighted = 0.0f;
    float Total    = 0.0f;
    for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
        for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
            for (uint32_t X = 0u; X < CoarseExtent; ++X)
            {
                const float Reading = Field.Smoke[VoxelIndex(X, Y, Z)];
                const uint32_t Along = Axis == 0u ? X : (Axis == 1u ? Y : Z);
                Weighted += Reading * static_cast<float>(Along);
                Total    += Reading;
            }
    return Total > 0.0f ? Weighted / Total : 0.0f;
}

}   // namespace


int main()
{
    std::printf("Native gas emitter component and gameplay hooks\n");
    try
    {

    //---------------------------------------------------------------------------------------------------------
    Banner("The cube root is ours, because multiplayer compares what it scales");
    {
        bool Agrees = true;
        for (uint32_t Step = 1u; Step <= 400u; ++Step)
        {
            const float Volume = static_cast<float>(Step) * 0.01f;
            const float Ours   = CubeRootOf(Volume);
            const float Theirs = std::cbrt(Volume);
            if (std::fabs(Ours - Theirs) > 1.0e-4f * Theirs) Agrees = false;
        }
        Check(Agrees, "an exponent-seeded four Newton steps match std::cbrt to a part in ten thousand over 0.01 to 4 m³");
        Check(CubeRootOf(0.0f) == 0.0f && CubeRootOf(-1.0f) == 0.0f, "and nothing negative or empty gets a radius");
        Check(CubeRootOf(0.125f) == CubeRootOf(0.125f), "the same volume answers the same way twice");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("A fracture piece asks for dust proportioned to itself");
    {
        GasFractureSeparation Small;
        Small.Carrier      = 7u;
        Small.PieceVolume  = 0.008f;    // a brick
        Small.PartingSpeed = 1.2f;

        GasFractureSeparation Large = Small;
        Large.PieceVolume = 0.064f;     // eight bricks

        const GasEmitterComponent One = ConstructFractureEmitter(Small);
        const GasEmitterComponent Two = ConstructFractureEmitter(Large);

        Check(std::strcmp(One.Preset, "brick_fracture_dust") == 0, "it carries the brick dust preset by identity");
        Check(!One.Continuous && One.Requests == 1u, "as a one-shot owing exactly one firing");
        Check(One.Carrier == 7u, "attached to the piece, so the emitter follows whatever the piece does next");
        Check(std::fabs(Two.Radius - One.Radius * 2.0f) < 0.01f,
              "eight times the volume is twice the radius - the relationship between a brick and a wall");

        GasFractureSeparation Boulder = Small;
        Boulder.PieceVolume = 40.0f;
        GasFractureSeparation Chip = Small;
        Chip.PieceVolume = 1.0e-6f;
        Check(ConstructFractureEmitter(Boulder).Radius <= GasFractureRadiusMost,
              "one enormous piece is still one piece and not a demolition");
        Check(ConstructFractureEmitter(Chip).Radius >= GasFractureRadiusLeast,
              "and a chip too small to see still gets a lattice it can be seen in");

        GasFractureSeparation Hard = Small;
        Hard.PartingSpeed = 6.0f;
        Check(ConstructFractureEmitter(Hard).Strength > One.Strength, "parting harder puts out more dust");

        const GasResidencyConsent Consent = ConstructFractureConsent();
        Check(GasRetires(Consent), "🔴 and the dust retires: forty pieces that each kept 40 MB is the whole ceiling");
        Check(Consent.Lifetime < 0.5f, "emission stops in a third of a second; the cloud outlives it by itself");

        Check(QualityForFracture(2.0f) == GasQuality::Mid,
              "standing beside the break buys Mid, never the one Hero slot - a puff cannot outrank a fire");
        Check(QualityForFracture(40.0f) == GasQuality::Flipbook, "and past twelve metres it is a card, not a solver");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("A tyre asks only while it is actually slipping");
    {
        GasTyreContact Gripping;
        Gripping.SlipRatio = 0.05f;
        Check(!TyreSmokes(Gripping), "a tyre driving normally is not an effect");
        Check(!ConstructTyreEmitter(Gripping).Enabled,
              "so four wheels on a cruising car hold no domain open, which is the entire concurrency ceiling");

        GasTyreContact Onset = Gripping;
        Onset.SlipRatio = GasTyreSlipOnset + 0.001f;
        Check(ConstructTyreEmitter(Onset).Strength < 0.02f,
              "crossing the threshold starts from nothing rather than switching on at full rate");

        GasTyreContact Hard = Gripping;
        Hard.SlipRatio = 0.6f;
        Hard.Forward[0] = 14.0f;
        const GasEmitterComponent Burnout = ConstructTyreEmitter(Hard);
        Check(Burnout.Enabled && Burnout.Strength > 0.4f, "a proper burnout is at strength");
        Check(Burnout.Spread == GasEmitterSpread::Disc, "laid flat as a contact patch, not stood up as a column");

        GasTyreContact Loaded = Hard;
        Loaded.NormalLoad = 8000.0f;
        Check(ConstructTyreEmitter(Loaded).Strength > Burnout.Strength, "and a loaded tyre smokes harder");

        GasTyreContact Light = Hard;
        Light.NormalLoad = 1.0f;
        Check(!TyreSmokes(Light), "a wheel in the air is not slipping, whatever the slip ratio says");

        const GasSettings Tuning = ConstructPresetSettings("tyre_burnout");
        const GasEmission Laid   = ResolveEmission(Burnout, Tuning);
        Check(std::fabs(Laid.Velocity[0]) > std::fabs(Laid.Velocity[2]) * 1.5f,
              "④ the cloud is dragged down the road harder than it climbs - a chimney bolted to a wheel is the tell");
        Check(Laid.Velocity[0] > 0.0f, "in the direction the patch was travelling, not the opposite one");
        Check(QualityForTyre(3.0f) == GasQuality::Near, "the player's own wheels buy Near, and four of them share");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("② Which domain holds it: the nearest that encloses it");
    {
        const GasDomainExtent Extents[2] = { Cube(10u, -12.0f, -12.0f, 0.0f, 24.0f),     // the street
                                             Cube(11u,  -2.0f,  -2.0f, 0.0f,  6.0f) };  // the room inside it

        const float Inside[3]  = { 1.0f, 0.0f, 1.0f };
        const float Street[3]  = { 9.0f, 0.0f, 1.0f };
        const float Outside[3] = { 80.0f, 0.0f, 1.0f };

        Check(ResolveHostExtent(Extents, 2u, Inside, 0.3f) == 1u, "an emitter in the room gets the room, not the street");
        Check(ResolveHostExtent(Extents, 2u, Street, 0.3f) == 0u, "one out on the street gets the street");
        Check(ResolveHostExtent(Extents, 2u, Outside, 0.3f) == kNoGasDomain, "and one in open ground gets nothing");

        const float Straddling[3] = { 3.8f, 0.0f, 1.0f };
        Check(!ExtentEncloses(Extents[1], Straddling, 0.6f),
              "an emitter whose plume straddles a wall does not fit - half an injection reads as weak, not misplaced");
        Check(ResolveHostExtent(Extents, 2u, Straddling, 0.6f) == 0u, "so it falls out to the street, which does hold it");

        const GasDomainExtent Same[2] = { Cube(20u, -4.0f, -4.0f, 0.0f, 8.0f), Cube(21u, -4.0f, -4.0f, 0.0f, 8.0f) };
        const float Centre[3] = { 0.0f, 0.0f, 1.0f };
        Check(ResolveHostExtent(Same, 2u, Centre, 0.2f) == 0u, "two identical domains tie to the lower index, every time");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("③ An emitter nobody authored a domain for still runs");
    {
        GasFractureSeparation Separation;
        Separation.Carrier     = 5u;
        Separation.Position[0] = 100.0f;
        Separation.Position[2] = 3.0f;
        Separation.PieceVolume = 0.2f;
        const GasEmitterComponent Emitter = ConstructFractureEmitter(Separation);
        const GasDomainExtent     Implicit = ConstructImplicitExtent(Emitter);

        Check(std::fabs(Implicit.Span - Emitter.Radius * GasImplicitSpanInRadii) < 0.001f,
              "the implicit cube is six radii across, which is the room a plume needs above its own source");
        Check(Implicit.Identity == 5u, "and it is named after the object that needed it");

        float Placement[3];
        ResolvePlacement(Emitter, Placement);
        Check(ExtentEncloses(Implicit, Placement, Emitter.Radius), "the emitter fits inside what was built for it");
        const float Up = (Placement[2] - Implicit.Origin[2]) / Implicit.Span;
        Check(Up > 0.15f && Up < 0.25f,
              "sitting a fifth of the way up rather than centred, because gas rises and half a lattice would be unused");

        GasEmitterComponent Tiny = Emitter;
        Tiny.Radius = 0.01f;
        Check(ConstructImplicitExtent(Tiny).Span >= GasImplicitSpanLeast, "a tiny source does not get a lattice of pure boundary");
        GasEmitterComponent Huge = Emitter;
        Huge.Radius = 40.0f;
        Check(ConstructImplicitExtent(Huge).Span <= GasImplicitSpanMost, "and a huge one is capped rather than quietly costing a level");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("① A one-shot fires exactly as often as it was asked to");
    {
        const GasDomainExtent Extent = Cube(1u, -4.0f, -4.0f, 0.0f, 8.0f);
        GasEmitterComponent Nine[1];
        Nine[0].Preset     = "brick_fracture_dust";
        Nine[0].Continuous = false;
        Nine[0].Requests   = 3u;
        Nine[0].CarrierAt[2] = 2.0f;

        GasEmitterRouting Routed[4];
        uint32_t Fired = 0u;
        for (uint32_t Tick = 0u; Tick < 6u; ++Tick)
            Fired += ResolveEmitters(Nine, 1u, &Extent, 1u, Routed, 4u);

        Check(Fired == 3u, "three requests over six ticks are three injections - not two, and not six");
        Check(Nine[0].Requests == 0u, "with nothing still owed afterwards");

        GasEmitterComponent Switched[1];
        Switched[0].Enabled = false;
        Check(ResolveEmitters(Switched, 1u, &Extent, 1u, Routed, 4u) == 0u, "a switched-off emitter contributes nothing");

        GasEmitterComponent Silent[1];
        Silent[0].Strength = 0.0f;
        Check(ResolveEmitters(Silent, 1u, &Extent, 1u, Routed, 4u) == 0u,
              "and neither does one at zero strength, which is every tyre that is not slipping");

        GasEmitterComponent Crowd[6];
        for (GasEmitterComponent& One : Crowd) One.CarrierAt[2] = 2.0f;
        Check(ResolveEmitters(Crowd, 6u, &Extent, 1u, Routed, 4u) == 4u,
              "overrunning the routing room stops writing and says how much it took, rather than writing past it");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("⑤ Resolving the same tick twice is bit-identical");
    {
        const GasDomainExtent Extent = Cube(1u, -4.0f, -4.0f, 0.0f, 8.0f);
        GasTyreContact Contact;
        Contact.SlipRatio  = 0.42f;
        Contact.Forward[0] = 11.0f;
        Contact.Position[2] = 0.1f;

        GasEmitterComponent First[1]  = { ConstructTyreEmitter(Contact) };
        GasEmitterComponent Second[1] = { ConstructTyreEmitter(Contact) };
        GasEmitterRouting Left[1], Right[1];
        Check(ResolveEmitters(First, 1u, &Extent, 1u, Left, 1u) == 1u, "the slipping tyre resolves");
        Check(ResolveEmitters(Second, 1u, &Extent, 1u, Right, 1u) == 1u, "twice");
        Check(std::memcmp(&Left[0].Emission, &Right[0].Emission, sizeof(GasEmission)) == 0,
              "to byte-identical emissions, which is what the solver's determinism contract rests on one layer up");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("A routing reaches the field it resolved to, and no other");
    {
        const GasDomainExtent Extent = Cube(1u, -4.0f, -4.0f, 0.0f, 8.0f);
        GasEmitterComponent Lamp[1];
        Lamp[0].Preset       = "lantern_flame";
        Lamp[0].CarrierAt[2] = 2.0f;
        Lamp[0].Radius       = 0.3f;

        GasEmitterRouting Routed[1];
        Check(ResolveEmitters(Lamp, 1u, &Extent, 1u, Routed, 1u) == 1u, "a lamp inside the domain resolves to it");
        Check(Routed[0].Host == 0u && !Routed[0].Implicit, "by authored index, with nothing implicit about it");

        CoarseGasField Field;
        ResetField(Field, Extent.Origin, Extent.Span);
        Check(InjectRouting(Field, Routed[0], 1.0f / 60.0f), "and injecting it into that field is admitted");
        Check(TotalSmoke(Field) > 0.0f, "putting smoke in it");

        CoarseGasField Elsewhere;
        const float Far[3] = { 400.0f, 0.0f, 0.0f };
        ResetField(Elsewhere, Far, 8.0f);
        Check(!InjectRouting(Elsewhere, Routed[0], 1.0f / 60.0f),
              "while the neighbouring domain refuses it - gas in the wrong room is the cost of being wrong here");
        Check(TotalSmoke(Elsewhere) == 0.0f, "and stays empty");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("④ Run for real: a burnout lays its cloud down the road");
    {
        // Two identical tyres, one standing still and one travelling. The difference between the clouds is the
        //    whole claim, and comparing them is the only way to make it without hard-coding where a plume ought
        //    to have reached after forty ticks of a solver nobody should have to predict by hand.
        const GasDomainExtent Extent = Cube(1u, -4.0f, -4.0f, 0.0f, 8.0f);
        float AlongRoad[2] = { 0.0f, 0.0f };
        float Climbed[2]   = { 0.0f, 0.0f };

        for (uint32_t Run = 0u; Run < 2u; ++Run)
        {
            CoarseGasField Field;
            ResetField(Field, Extent.Origin, Extent.Span);
            CoarseGasSettings Settings;

            GasTyreContact Contact;
            Contact.SlipRatio   = 0.7f;
            Contact.Position[2] = 0.3f;
            Contact.Forward[0]  = Run == 0u ? 0.0f : 12.0f;   // a burnout against the brakes, then a rolling one

            for (uint32_t Tick = 0u; Tick < 40u; ++Tick)
            {
                GasEmitterComponent Wheel[1] = { ConstructTyreEmitter(Contact) };
                GasEmitterRouting Routed[1];
                if (ResolveEmitters(Wheel, 1u, &Extent, 1u, Routed, 1u) == 1u)
                    InjectRouting(Field, Routed[0], CoarseStepInterval);
                AdvanceField(Field, Settings);
            }

            Check(TotalSmoke(Field) > 0.0f, Run == 0u ? "forty ticks of a stationary burnout fill the lattice"
                                                      : "and so do forty ticks of a rolling one");
            AlongRoad[Run] = SmokeCentroidAlong(Field, 0u);
            Climbed[Run]   = SmokeCentroidAlong(Field, 2u);
        }

        Check(AlongRoad[1] > AlongRoad[0] + 0.25f,
              "the travelling tyre's cloud sits further down the road than the stationary one's");
        Check(std::fabs(AlongRoad[0] - static_cast<float>(CoarseExtent) * 0.5f) < 0.6f,
              "while the stationary one stays over its own contact patch, as a burnout against the brakes does");
        Check(Climbed[1] < 3.0f,
              "and neither climbs: a disc source spends a fifth of its rise, which is why it is not a chimney");
    }

    Banner("A collapsing wall cannot evict the fire the player is standing in");
    {
        // Forty pieces come away twenty metres off, while a camp fire burns at arm's length. The governor sees
        //    one list and demotes from the back; the hook's own cap means the pieces never even ask for Hero.
        std::vector<GasDomainClaim> Claims;
        GasDomainClaim Fire;
        Fire.ViewerDistance = 2.0f;
        Claims.push_back(Fire);
        for (uint32_t Piece = 0u; Piece < 40u; ++Piece)
        {
            GasDomainClaim Dust;
            Dust.ViewerDistance = 20.0f + static_cast<float>(Piece) * 0.1f;
            Claims.push_back(Dust);
        }

        const GasBudgetCeiling Ceiling;
        Check(AssignQuality(Claims.data(), static_cast<uint32_t>(Claims.size()), Ceiling),
              "forty-one claims settle inside all three ceilings");
        Check(Claims[0].Assigned == GasQuality::Hero, "with the fire in front of the player still on Hero");
        Check(Claims[0].DemotionApplied == false, "and it never lost a rung to the wall at all");

        uint32_t Simulating = 0u;
        for (const GasDomainClaim& One : Claims)
            if (One.Assigned != GasQuality::Flipbook) ++Simulating;
        Check(Simulating <= Ceiling.DomainLimit, "while most of the dust became cards, which is what the ladder is for");

        Check(QualityForFracture(20.0f) == GasQuality::Flipbook,
              "🔴 and the hook had already capped them there before the governor was asked, at no cost at all");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The seam is real: the shipped wheel telemetry fills the contact report unchanged");
    {
        // Frontier::Vehicle::VehicleTelemetry is what the vehicle solver already publishes every step. Nothing
        //    in it was added for gas, which is the whole claim GasTyreContact makes about itself.
        Vehicle::VehicleTelemetry Car;
        Car.WheelCount   = 4u;
        Car.ForwardSpeed = 18.0f;
        Car.Wheels[2].ContactPoint = { 2.5f, -0.8f, 0.04f };
        Car.Wheels[2].VerticalLoad = 3100.0f;
        Car.Wheels[2].SlipRatio    = 0.62f;
        Car.Wheels[2].InContact    = true;

        const GasTyreContact Read = ReadTyreContact(Car, 2u);
        Check(Read.Position[0] == 2.5f && Read.Position[2] == 0.04f,
              "the contact patch crosses unchanged - both layers put +Z up, so there is no axis to get wrong");
        Check(Read.NormalLoad == 3100.0f && Read.SlipRatio == 0.62f, "and so do the load and the slip");
        Check(Read.Carrier == 2u, "the wheel index is the carrier, so four corners are four emitters and not one");
        Check(Read.Forward[0] > 17.0f && std::fabs(Read.Forward[2]) < 1e-5f,
              "the cloud is dragged along the hub's own axis at the speed the car is making");

        Check(TyreSmokes(Read) && ConstructWheelEmitter(Car, 2u).Enabled,
              "a wheel spinning that hard smokes, through the adapter and the hook together");

        Car.Wheels[2].SlipRatio = -0.62f;
        Check(ReadTyreContact(Car, 2u).SlipRatio == 0.62f,
              "a wheel locked under braking reports negative slip and lays down exactly the same smoke");

        Car.Wheels[2].InContact = false;
        Check(ReadTyreContact(Car, 2u).NormalLoad == 0.0f && !ConstructWheelEmitter(Car, 2u).Enabled,
              "④ a wheel in the air carries no load, so the call site needs no check of its own");

        Check(ReadTyreContact(Car, 7u).NormalLoad == 0.0f,
              "and asking for a corner the car does not have answers a quiet patch rather than reading past the end");
    }

    }
    catch (const std::exception& Failure)
    {
        std::printf("\nFAILED after %u checks: %s\n", Checks, Failure.what());
        return 1;
    }

    std::printf("\nPASS %u\n", Checks);
    return 0;
}
