//============================================================================================================================================
// 📦 Scratchpad/AtmosphericOpticsProof.cpp — the bow is where optics says it is
//============================================================================================================================================
// Celestial step 7. The rainbow is checked against published angles rather than against itself, because the whole
//    argument for deriving it is that the derivation produces facts a hand-tuned arc cannot.

#include "DisplayPresentation/AtmosphericOptics.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace Frontier;

namespace {
int Failures = 0;
void Check(const char* Label, double Actual, double Expected, double Tolerance, const char* Unit)
{
    const double Error = std::fabs(Actual - Expected);
    const bool Ok = Error <= Tolerance;
    if (!Ok) ++Failures;
    std::printf("  %-56s %8.3f vs %8.3f %-4s err %6.3f  %s\n",
                Label, Actual, Expected, Unit, Error, Ok ? "PASS" : "FAIL");
}
void Expect(bool Condition, const char* What)
{
    if (!Condition) ++Failures;
    std::printf("  %-68s %s\n", What, Condition ? "PASS" : "FAIL");
}
constexpr double kPi = 3.14159265358979323846;
double Degrees(double R) { return R * 180.0 / kPi; }
} // namespace

int main()
{
    std::printf("\nAtmosphericOptics — the rainbow, from Descartes\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    // ── ① refractive index ─────────────────────────────────────────────────────────────────────────────────────
    std::printf("1. water disperses, which is why there are colours at all\n");
    Check("n at 700 nm (red)",    AtmosphericOptics::WaterIndex(700.0f), 1.3306, 0.002, "-");
    Check("n at 400 nm (violet)", AtmosphericOptics::WaterIndex(400.0f), 1.3433, 0.002, "-");
    Expect(AtmosphericOptics::WaterIndex(400.0f) > AtmosphericOptics::WaterIndex(700.0f),
           "violet is bent more than red");

    // ── ② the Descartes angles ─────────────────────────────────────────────────────────────────────────────────
    // Published: primary red 42.4 deg, violet 40.7; secondary red 50.4, violet 53.4.
    std::printf("\n2. the bows sit at their published angles\n");
    Check("primary red 700 nm",     Degrees(AtmosphericOptics::RainbowAngle(700.0f, 1u)), 42.4, 0.3, "deg");
    Check("primary violet 400 nm",  Degrees(AtmosphericOptics::RainbowAngle(400.0f, 1u)), 40.7, 0.3, "deg");
    Check("secondary red 700 nm",   Degrees(AtmosphericOptics::RainbowAngle(700.0f, 2u)), 50.4, 0.3, "deg");
    Check("secondary violet 400 nm",Degrees(AtmosphericOptics::RainbowAngle(400.0f, 2u)), 53.4, 0.3, "deg");

    // ── ③ the reversal ─────────────────────────────────────────────────────────────────────────────────────────
    // The property a hand-drawn arc almost always gets wrong: the second bow's colours run the other way, because
    // the extra internal reflection flips the ordering. It is not an artistic choice.
    std::printf("\n3. the secondary bow's colours are reversed\n");
    {
        const float PrimaryRed    = AtmosphericOptics::RainbowAngle(700.0f, 1u);
        const float PrimaryViolet = AtmosphericOptics::RainbowAngle(400.0f, 1u);
        const float SecondRed     = AtmosphericOptics::RainbowAngle(700.0f, 2u);
        const float SecondViolet  = AtmosphericOptics::RainbowAngle(400.0f, 2u);
        std::printf("     primary:   red %.2f deg is OUTSIDE violet %.2f deg\n",
                    Degrees(PrimaryRed), Degrees(PrimaryViolet));
        std::printf("     secondary: red %.2f deg is INSIDE violet %.2f deg\n",
                    Degrees(SecondRed), Degrees(SecondViolet));
        Expect(PrimaryRed > PrimaryViolet, "primary: red on the outside");
        Expect(SecondRed < SecondViolet,   "secondary: red on the inside — the order is flipped");
        Expect(SecondViolet > PrimaryRed,  "the secondary bow lies outside the primary");
    }

    // ── ④ Alexander's band ─────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n4. Alexander's dark band lies between the bows\n");
    {
        RainbowSettings Settings{};
        const float Inside  = 0.68f;   // ~39 deg, inside the primary
        const float Between = 0.80f;   // ~46 deg, between the two
        const float Outside = 0.97f;   // ~56 deg, outside the secondary
        const float A = AtmosphericOptics::AlexanderAttenuation(Settings, Inside);
        const float B = AtmosphericOptics::AlexanderAttenuation(Settings, Between);
        const float C = AtmosphericOptics::AlexanderAttenuation(Settings, Outside);
        std::printf("     attenuation: inside %.3f, between %.3f, outside %.3f\n", A, B, C);
        Expect(B < A && B < C, "the sky between the bows is darker than either side");
    }

    // ── ⑤ the bow only appears where it should ─────────────────────────────────────────────────────────────────
    std::printf("\n5. a bow needs rain, a low sun, and the right direction\n");
    {
        RainbowSettings Settings{};
        // Sun low in the east; the antisolar point is then low in the west.
        float Sun[3] = { 0.0f, -0.30f, 0.20f };
        const float L = std::sqrt(Sun[0]*Sun[0] + Sun[1]*Sun[1] + Sun[2]*Sun[2]);
        for (int I = 0; I < 3; ++I) Sun[I] /= L;

        float Rgb[3];
        // Straight at the antisolar point: inside the bow, no colour.
        float Anti[3] = { -Sun[0], -Sun[1], -Sun[2] };
        AtmosphericOptics::Rainbow(Settings, Anti, Sun, 1.0f, Rgb);
        Expect(Rgb[0] + Rgb[1] + Rgb[2] < 1e-4f, "nothing at the antisolar point itself");

        // 42 degrees off it: the bow.
        float OnBow[3];
        {
            // Rotate the antisolar direction by 42 degrees in the vertical plane.
            const float A = 42.0f * static_cast<float>(kPi) / 180.0f;
            const float Up[3] = { 0.0f, 0.0f, 1.0f };
            float Side[3] = { Anti[1] * Up[2] - Anti[2] * Up[1],
                              Anti[2] * Up[0] - Anti[0] * Up[2],
                              Anti[0] * Up[1] - Anti[1] * Up[0] };
            const float SL = std::sqrt(Side[0]*Side[0] + Side[1]*Side[1] + Side[2]*Side[2]);
            for (int I = 0; I < 3; ++I) Side[I] /= SL;
            float Perp[3] = { Side[1] * Anti[2] - Side[2] * Anti[1],
                              Side[2] * Anti[0] - Side[0] * Anti[2],
                              Side[0] * Anti[1] - Side[1] * Anti[0] };
            for (int I = 0; I < 3; ++I) OnBow[I] = Anti[I] * std::cos(A) + Perp[I] * std::sin(A);
        }
        AtmosphericOptics::Rainbow(Settings, OnBow, Sun, 1.0f, Rgb);
        const float OnBowSum = Rgb[0] + Rgb[1] + Rgb[2];
        std::printf("     at 42 deg from the antisolar point: rgb %.4f %.4f %.4f\n", Rgb[0], Rgb[1], Rgb[2]);
        Expect(OnBowSum > 0.05f, "there is a bow at 42 degrees");

        // No rain, no bow — however good the geometry.
        AtmosphericOptics::Rainbow(Settings, OnBow, Sun, 0.0f, Rgb);
        Expect(Rgb[0] + Rgb[1] + Rgb[2] < 1e-6f, "no rain means no bow");

        // Sun high overhead: the bow is below the horizon and must not appear.
        float High[3] = { 0.0f, 0.0f, 1.0f };
        AtmosphericOptics::Rainbow(Settings, OnBow, High, 1.0f, Rgb);
        std::printf("     with the sun overhead: rgb %.4f %.4f %.4f\n", Rgb[0], Rgb[1], Rgb[2]);
        Expect(Rgb[0] + Rgb[1] + Rgb[2] < OnBowSum,
               "a high sun gives no bow at that direction");
    }

    // ── ⑥ aerial perspective ───────────────────────────────────────────────────────────────────────────────────
    std::printf("\n6. distance takes the colour of the air\n");
    {
        AtmosphereMedium Medium{};
        AtmosphereLight Light{};
        Light.Direction[0] = 0.0f; Light.Direction[1] = 0.6f; Light.Direction[2] = 0.8f;
        const float Direction[3] = { 0.0f, 1.0f, 0.0f };
        const float Sky[3] = { 0.22f, 0.32f, 0.53f };

        float Near[3], NearScatter[3], Far[3], FarScatter[3];
        AtmosphericOptics::AerialPerspective(Medium, Light, 100.0f, 2.0f, 2.0f, Direction, Sky, Near, NearScatter);
        AtmosphericOptics::AerialPerspective(Medium, Light, 40000.0f, 2.0f, 2.0f, Direction, Sky, Far, FarScatter);
        std::printf("     100 m : transmittance %.4f %.4f %.4f\n", Near[0], Near[1], Near[2]);
        std::printf("     40 km : transmittance %.4f %.4f %.4f\n", Far[0], Far[1], Far[2]);
        Expect(Far[0] < Near[0] && Far[1] < Near[1] && Far[2] < Near[2],
               "further is hazier");
        Expect(Far[2] < Far[0], "blue is extinguished fastest, so distant things go warm then pale");
        Expect(FarScatter[2] > NearScatter[2], "and the air's own light builds with distance");
        Expect(Near[0] > 0.99f, "at 100 m there is essentially no haze");
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the optics behave" : "  THE OPTICS DO NOT BEHAVE");
    return Failures == 0 ? 0 : 1;
}
