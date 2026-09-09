//============================================================================================================================================
//                                                    LIGHTPROJECTIONTEST.CPP
//============================================================================================================================================
// 🧩 The light-contribution gate. Proves the panel becomes a real emitter the renderer can sample — not merely
//    that a quad was appended.
//
//    The check that matters is the last one: after Finalise, the scene's luminaire table must contain the proxy's
//    triangles. A quad registered too late, or with zero area, or with a black material, still shows up in the
//    instance list and lights nothing. Only the luminaire table decides whether a surface can be sampled as a
//    light source, so that is what is asserted.
//
//    Build: bash Scratchpad/CheckLightProjection.sh

#include "SpatialInterface/InterfaceLightProjection.h"
#include "SpatialInterface/InterfaceSequence.h"
#include "SpatialInterface/InterfaceStructure.h"
#include "GeometricRaster/SceneStructure.h"

#include <cmath>
#include <cstdio>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-66s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

void CheckNear(const char* Name, float Value, float Target, float Tolerance)
{
    const bool Ok = std::fabs(Value - Target) <= Tolerance;
    std::printf("  %-66s %s  (%.4f vs %.4f)\n", Name, Ok ? "PASS" : "FAIL", Value, Target);
    if (!Ok) ++Failures;
}

// A panel with one lit figure of a chosen colour, and one dark figure that must not contribute.
struct Panel
{
    Frontier::InterfaceStructure Structure;
    Frontier::InterfaceSequence  Composition;

    Panel(uint32_t LitTint, float EmissiveWeight, float LitHalfSize)
    {
        Frontier::InterfaceFigure Housing;
        Housing.HalfWidth      = 0.10f;
        Housing.HalfHeight     = 0.06f;
        Housing.TintOverride   = 0xFF101010u;      // near-black bezel
        Housing.EmissiveWeight = 0.0f;             // pure albedo: reflects, never radiates
        (void)Structure.Construct(Housing);

        Frontier::InterfaceFigure Lit;
        Lit.HalfWidth      = LitHalfSize;
        Lit.HalfHeight     = LitHalfSize;
        Lit.TintOverride   = LitTint;
        Lit.EmissiveWeight = EmissiveWeight;
        Lit.Placement.Origin = Frontier::PlaneOrigin{ 0.0f, 0.0f, 0.001f };
        (void)Structure.Construct(Lit);

        Frontier::InterfaceViewConfiguration View;
        View.EyeZ = 1.0f; View.ForwardY = 1.0f;
        Composition.AssignView(View);
        Composition.Advance(Structure, 0.0);
    }
};

} // namespace

int main()
{
    std::printf("\n=== interface light contribution gate ===\n\n");

    constexpr float kPanelArea = 0.10f * 2.0f * 0.06f * 2.0f;   // the housing's face, 0.20 × 0.12 m

    //──────────────────────────────────────────────────────────────────────────
    // ① Radiance follows the figures.
    //──────────────────────────────────────────────────────────────────────────
    {
        Panel Red(0xFF0000FFu, 1.0f, 0.03f);      // RGBA8 little-endian: R in the low byte
        const Frontier::PanelRadiance R =
            Frontier::InterfaceLightProjection::MeasureRadiance(Red.Structure, Red.Composition, kPanelArea);

        std::printf("  red panel: rgb (%.4f %.4f %.4f), %u contributor(s), coverage %.1f%%\n",
                    R.Red, R.Green, R.Blue, R.Contributors, R.Coverage() * 100.0);

        CheckTrue("a lit figure contributes",              R.Contributors == 1u);
        CheckTrue("the dark bezel does not contribute",    R.Contributors == 1u);
        CheckTrue("red dominates a red panel",             R.Red > R.Green * 4.0f && R.Red > R.Blue * 4.0f);
        CheckTrue("the panel emits something",             R.Red > 0.0f);
        CheckTrue("coverage is a sane fraction",           R.Coverage() > 0.0f && R.Coverage() <= 1.0f);

        // Overlapping figures can sum past the panel face; coverage must never exceed 1 or a busy panel would
        //    light the room harder than a solid white one.
        Panel Crowded(0xFFFFFFFFu, 1.0f, 0.30f);   // a lit figure far larger than the housing
        const Frontier::PanelRadiance Over =
            Frontier::InterfaceLightProjection::MeasureRadiance(Crowded.Structure, Crowded.Composition, kPanelArea);
        std::printf("  overlapping figures: lit area %.4f vs panel %.4f -> coverage %.1f%%\n",
                    Over.LitArea, Over.PanelArea, Over.Coverage() * 100.0);
        CheckTrue("coverage is clamped to 1 when figures overlap", Over.Coverage() <= 1.0f);
    }

    {
        Panel Blue(0xFFFF0000u, 1.0f, 0.03f);     // blue in the high colour byte
        const Frontier::PanelRadiance B =
            Frontier::InterfaceLightProjection::MeasureRadiance(Blue.Structure, Blue.Composition, kPanelArea);
        CheckTrue("blue dominates a blue panel", B.Blue > B.Red * 4.0f && B.Blue > B.Green * 4.0f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ② EmissiveWeight is honoured — an albedo-only panel radiates nothing.
    //──────────────────────────────────────────────────────────────────────────
    {
        Panel Dark(0xFFFFFFFFu, 0.0f, 0.03f);
        const Frontier::PanelRadiance D =
            Frontier::InterfaceLightProjection::MeasureRadiance(Dark.Structure, Dark.Composition, kPanelArea);
        CheckTrue("a pure-albedo panel radiates nothing", D.Contributors == 0u && D.Red == 0.0f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ③ A bigger lit area lights more, and coverage scales it.
    //──────────────────────────────────────────────────────────────────────────
    {
        Panel Small(0xFFFFFFFFu, 1.0f, 0.02f);
        Panel Large(0xFFFFFFFFu, 1.0f, 0.05f);
        const Frontier::PanelRadiance S =
            Frontier::InterfaceLightProjection::MeasureRadiance(Small.Structure, Small.Composition, kPanelArea);
        const Frontier::PanelRadiance L =
            Frontier::InterfaceLightProjection::MeasureRadiance(Large.Structure, Large.Composition, kPanelArea);
        std::printf("  small lamp %.4f vs large lamp %.4f\n", S.Red, L.Red);
        CheckTrue("a larger lit area emits more light", L.Red > S.Red * 2.0f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ④ Off registers nothing; unavailable tiers say so rather than pretending.
    //──────────────────────────────────────────────────────────────────────────
    {
        Panel Bright(0xFFFFFFFFu, 1.0f, 0.05f);
        const Frontier::PanelRadiance R =
            Frontier::InterfaceLightProjection::MeasureRadiance(Bright.Structure, Bright.Composition, kPanelArea);

        Frontier::PanelProxyRequest Request;
        Request.CentreY = 1.5f;
        Request.RightX = 0.10f; Request.RightY = 0.0f; Request.RightZ = 0.0f;
        Request.UpX = 0.0f;     Request.UpY = 0.0f;    Request.UpZ = 0.06f;

        Frontier::SceneStructure Scene;
        Request.Tier = Frontier::InterfaceFidelityTier::Off;
        CheckTrue("Off registers no proxy",
                  Frontier::InterfaceLightProjection::ComposeProxy(Scene, Request, R) == 0xFFFFFFFFu);
        CheckTrue("Off leaves the scene untouched", Scene.QueryInstances().empty());

        Request.Tier = Frontier::InterfaceFidelityTier::High;
        CheckTrue("High reports itself unavailable",
                  !Frontier::InterfaceLightProjection::IsTierAvailable(Frontier::InterfaceFidelityTier::High));
        CheckTrue("an unavailable tier registers nothing rather than silently using Low",
                  Frontier::InterfaceLightProjection::ComposeProxy(Scene, Request, R) == 0xFFFFFFFFu);

        CheckTrue("Off and Low are available",
                  Frontier::InterfaceLightProjection::IsTierAvailable(Frontier::InterfaceFidelityTier::Off) &&
                  Frontier::InterfaceLightProjection::IsTierAvailable(Frontier::InterfaceFidelityTier::Low));
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑤ A dark panel is not registered — a black emitter is pure waste.
    //──────────────────────────────────────────────────────────────────────────
    {
        Panel Dark(0xFFFFFFFFu, 0.0f, 0.03f);
        const Frontier::PanelRadiance D =
            Frontier::InterfaceLightProjection::MeasureRadiance(Dark.Structure, Dark.Composition, kPanelArea);
        Frontier::PanelProxyRequest Request;
        Request.Tier = Frontier::InterfaceFidelityTier::Low;
        Request.RightX = 0.10f; Request.UpZ = 0.06f;
        Frontier::SceneStructure Scene;
        CheckTrue("a panel emitting nothing is not registered",
                  Frontier::InterfaceLightProjection::ComposeProxy(Scene, Request, D) == 0xFFFFFFFFu);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑥ A degenerate quad is refused rather than producing a zero-area luminaire.
    //──────────────────────────────────────────────────────────────────────────
    {
        Panel Bright(0xFFFFFFFFu, 1.0f, 0.05f);
        const Frontier::PanelRadiance R =
            Frontier::InterfaceLightProjection::MeasureRadiance(Bright.Structure, Bright.Composition, kPanelArea);
        Frontier::PanelProxyRequest Request;
        Request.Tier = Frontier::InterfaceFidelityTier::Low;
        // Parallel half-axes: no area, no normal.
        Request.RightX = 0.10f; Request.RightY = 0.0f; Request.RightZ = 0.0f;
        Request.UpX    = 0.20f; Request.UpY    = 0.0f; Request.UpZ    = 0.0f;
        Frontier::SceneStructure Scene;
        CheckTrue("a degenerate quad is refused (no NaN luminaire)",
                  Frontier::InterfaceLightProjection::ComposeProxy(Scene, Request, R) == 0xFFFFFFFFu);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑦ THE ONE THAT MATTERS: after Finalise the proxy is in the LUMINAIRE table.
    //──────────────────────────────────────────────────────────────────────────
    // Being in the instance list is not enough. The light sampler only ever looks at Luminaires, so a proxy that
    //    is drawn but not sampled would light nothing while looking entirely correct in the scene dump.
    {
        Panel Bright(0xFF20C0FFu, 1.0f, 0.05f);
        const Frontier::PanelRadiance R =
            Frontier::InterfaceLightProjection::MeasureRadiance(Bright.Structure, Bright.Composition, kPanelArea);

        Frontier::SceneStructure Scene;
        Frontier::PanelProxyRequest Request;
        Request.Tier = Frontier::InterfaceFidelityTier::Low;
        Request.CentreY = 1.5f; Request.CentreZ = 1.3f;
        Request.RightX = 0.10f; Request.RightY = 0.0f; Request.RightZ = 0.0f;
        Request.UpX = 0.0f;     Request.UpY = 0.0f;    Request.UpZ = 0.06f;
        Request.Gain = 8.0f;

        const uint32_t Instance = Frontier::InterfaceLightProjection::ComposeProxy(Scene, Request, R);
        CheckTrue("Low registers the proxy instance", Instance != 0xFFFFFFFFu);

        Scene.Finalise(64u, nullptr);

        std::printf("  after Finalise: %zu instances, %u triangles, %zu luminaires\n",
                    Scene.QueryInstances().size(), Scene.QueryTriangleCount(), Scene.QueryLuminaires().size());

        CheckTrue("the proxy contributes two triangles", Scene.QueryTriangleCount() == 2u);
        CheckTrue("THE PANEL IS IN THE LUMINAIRE TABLE",  Scene.QueryLuminaires().size() == 2u);

        // Each luminaire must carry a real area, or the area sampler divides by zero.
        bool AreasValid = true;
        for (const Frontier::LuminaireRecord& L : Scene.QueryLuminaires())
            if (!(L.Area > 0.0f)) AreasValid = false;
        CheckTrue("every luminaire has a positive area", AreasValid);

        // The quad's total area must match the requested half-axes: 0.20 × 0.12 m.
        float Total = 0.0f;
        for (const Frontier::LuminaireRecord& L : Scene.QueryLuminaires()) Total += L.Area;
        CheckNear("the emitter area matches the panel face", Total, 0.20f * 0.12f, 1.0e-5f);

        // And the instance is flagged emissive, which is what marks it for the sampler.
        CheckTrue("the proxy instance is flagged emissive",
                  (Scene.QueryInstances()[Instance].Flags & Frontier::InstanceFlagEmissive) != 0u);
    }

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
