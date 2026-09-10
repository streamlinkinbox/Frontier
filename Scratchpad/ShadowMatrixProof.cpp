//============================================================================================================================================
//  ShadowMatrixProof.cpp — does the PCSS penumbra survive a light that is not pointing straight down?
//============================================================================================================================================
// ShadowSample.slang recovered the light's half-angle from the world→clip matrix:
//
//      float TanHalf = 1.0 / max(ShadowLightClip[Tap][1][1], 1e-6);
//
// with the comment "recovered from the projection matrix rather than passed again, so the two cannot drift apart".
// That reasoning holds for a bare PROJECTION matrix, where element [1][1] really is 1/tan(half). ShadowLightClip is
// not one: it is Projection · View, so
//
//      M[1][1] = (±1 / tanHalf) · Up.y
//
// and the recovered angle is only correct when the light's up axis happens to be world Y. This program builds the
// matrix exactly as the host does, for a spread of light directions, and prints what the shader would have
// recovered against the truth. Run it and the failure is not subtle.
//
//   g++ -std=c++20 -O2 -Wall -Wextra -o /tmp/shadowmatrix Scratchpad/ShadowMatrixProof.cpp && /tmp/shadowmatrix

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {

constexpr float kPi         = 3.14159265358979323846f;
constexpr float kShadowHalf = 65.0f;    // [deg] matches VisibilityRaster::kShadowHalf
constexpr float kNear       = 0.02f;
constexpr float kFar        = 100.0f;

struct Vec3 { float X, Y, Z; };

Vec3 Subtract(Vec3 A, Vec3 B)  { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
Vec3 Cross(Vec3 A, Vec3 B)     { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
float Length(Vec3 A)           { return std::sqrt(A.X * A.X + A.Y * A.Y + A.Z * A.Z); }
Vec3 Normalise(Vec3 A)         { const float L = Length(A); return L > 0.0f ? Vec3{ A.X / L, A.Y / L, A.Z / L } : Vec3{ 0.0f, 0.0f, -1.0f }; }

// Column-major, Columns[column][row] — the layout Matrix4x4 uses and the layout GLSL's mat4 expects.
struct Matrix { float Columns[4][4]{}; };

// The light basis, lifted term for term from VisibilityRaster::RasterizeShadow so the proof cannot flatter itself
//    with a different construction than the one that ships.
void LightBasis(Vec3 Tap, Vec3 Centre, Vec3& Right, Vec3& Up, Vec3& Direction)
{
    Direction = Normalise(Subtract(Centre, Tap));
    Vec3 Reference{ 0.0f, 0.0f, 1.0f };
    if (Direction.X * Direction.X + Direction.Y * Direction.Y < 0.01f) Reference = { 0.0f, 1.0f, 0.0f };
    Right = Normalise(Cross(Direction, Reference));
    Up    = Cross(Right, Direction);
}

// world → light clip. Ordinary depth (near→0, far→1) with LESS, as ShadowRaster.vert documents.
Matrix LightClip(Vec3 Tap, Vec3 Centre)
{
    Vec3 R, U, D;
    LightBasis(Tap, Centre, R, U, D);
    const float TanHalf = std::tan(kShadowHalf * kPi / 180.0f);
    const float Focal   = 1.0f / TanHalf;
    const float Range   = kFar / (kFar - kNear);

    // Rows of Projection · View. View rows are the basis vectors; the projection scales x and y by the focal
    //    length and maps z into [0,1]. Y is negated once for Vulkan's downward NDC.
    const float Row0[4] = {  Focal * R.X,  Focal * R.Y,  Focal * R.Z, -Focal * (R.X * Tap.X + R.Y * Tap.Y + R.Z * Tap.Z) };
    const float Row1[4] = { -Focal * U.X, -Focal * U.Y, -Focal * U.Z,  Focal * (U.X * Tap.X + U.Y * Tap.Y + U.Z * Tap.Z) };
    const float Row2[4] = {  Range * D.X,  Range * D.Y,  Range * D.Z, -Range * (D.X * Tap.X + D.Y * Tap.Y + D.Z * Tap.Z) - Range * kNear };
    const float Row3[4] = {          D.X,          D.Y,          D.Z,         -(D.X * Tap.X + D.Y * Tap.Y + D.Z * Tap.Z) };

    Matrix M;
    for (int C = 0; C < 4; ++C)
    {
        M.Columns[C][0] = Row0[C];
        M.Columns[C][1] = Row1[C];
        M.Columns[C][2] = Row2[C];
        M.Columns[C][3] = Row3[C];
    }
    return M;
}

} // namespace

// Does the matrix actually project? A point at the tap's aim direction must land at the centre of the map, a point
//    behind the light must have w <= 0, and a point at distance d must reconstruct to d metres through
//    ShadowLinearDepth — that last one is what the PCSS bias and penumbra are measured in.
float LinearDepth(float Stored, float Near, float Far)
{
    return (Near * Far) / std::fmax(Far - Stored * (Far - Near), 1e-6f);
}

int ProjectionChecks()
{
    std::printf("\nProjection behaviour (light at (0,0,4) aimed at the origin, near %.2f far %.0f)\n\n",
                static_cast<double>(kNear), static_cast<double>(kFar));
    const Vec3 Tap{ 0.0f, 0.0f, 4.0f }, Centre{ 0.0f, 0.0f, 0.0f };
    const Matrix M = LightClip(Tap, Centre);

    const auto Project = [&](Vec3 P, float& U, float& V, float& W, float& Metres)
    {
        float C[4];
        for (int R = 0; R < 4; ++R)
            C[R] = M.Columns[0][R] * P.X + M.Columns[1][R] * P.Y + M.Columns[2][R] * P.Z + M.Columns[3][R];
        W = C[3];
        U = V = Metres = 0.0f;
        if (W <= 0.0f) return;
        U = (C[0] / W) * 0.5f + 0.5f;
        V = (C[1] / W) * 0.5f + 0.5f;
        Metres = LinearDepth(C[2] / W, kNear, kFar);
    };

    int Bad = 0;
    struct Probe { const char* Name; Vec3 P; };
    const Probe Probes[] = {
        { "the aim point, 4 m below the light", { 0.0f, 0.0f, 0.0f } },
        { "1 m below the light",                { 0.0f, 0.0f, 3.0f } },
        { "behind the light",                   { 0.0f, 0.0f, 6.0f } },
    };
    std::printf("  %-36s %8s %8s %10s %10s\n", "point", "u", "v", "w", "metres");
    for (const Probe& X : Probes)
    {
        float U, V, W, Metres;
        Project(X.P, U, V, W, Metres);
        std::printf("  %-36s %8.4f %8.4f %10.3f %10.4f\n", X.Name,
                    static_cast<double>(U), static_cast<double>(V), static_cast<double>(W), static_cast<double>(Metres));
    }

    // ① the aim point must land dead centre
    { float U, V, W, D; Project(Probes[0].P, U, V, W, D);
      if (std::fabs(U - 0.5f) > 1e-4f || std::fabs(V - 0.5f) > 1e-4f) { std::printf("  FAIL: the aim point is not at the map centre\n"); ++Bad; }
      if (std::fabs(D - 4.0f) > 1e-3f) { std::printf("  FAIL: the aim point reconstructs to %.4f m, not 4 m\n", static_cast<double>(D)); ++Bad; } }
    // ② a nearer point must reconstruct nearer, in metres
    { float U, V, W, D; Project(Probes[1].P, U, V, W, D);
      if (std::fabs(D - 1.0f) > 1e-3f) { std::printf("  FAIL: the 1 m point reconstructs to %.4f m\n", static_cast<double>(D)); ++Bad; } }
    // ③ behind the light must be rejected by w <= 0, which is how the sampler returns "lit"
    { float U, V, W, D; Project(Probes[2].P, U, V, W, D);
      if (W > 0.0f) { std::printf("  FAIL: a point behind the light has w = %.3f > 0\n", static_cast<double>(W)); ++Bad; } }

    if (!Bad) std::printf("\n  centre, depth-in-metres and behind-the-light all correct.\n");
    return Bad;
}

int main()
{
    const float Truth = std::tan(kShadowHalf * kPi / 180.0f);

    std::printf("\nPCSS half-angle recovery: 1 / ShadowLightClip[1][1] against the real tan(half)\n");
    std::printf("(kShadowHalf = %.0f deg, so every row should read %.4f)\n\n", static_cast<double>(kShadowHalf), static_cast<double>(Truth));
    std::printf("  %-34s %10s %10s %12s   %s\n", "light aimed", "recovered", "truth", "penumbra", "verdict");

    struct Case { const char* Name; Vec3 Tap; Vec3 Centre; };
    const Case Cases[] = {
        { "straight down (ceiling lamp)",   { 0.0f, 0.0f, 4.0f }, { 0.0f, 0.0f,  0.0f } },
        { "straight up",                    { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f,  4.0f } },
        { "horizontally along +X",          { 4.0f, 0.0f, 1.0f }, { 0.0f, 0.0f,  1.0f } },
        { "horizontally along +Y",          { 0.0f, 4.0f, 1.0f }, { 0.0f, 0.0f,  1.0f } },
        { "45 deg down the +X axis",        { 4.0f, 0.0f, 4.0f }, { 0.0f, 0.0f,  0.0f } },
        { "an awkward diagonal",            { 3.0f, 2.5f, 3.5f }, { 0.2f, -0.4f, 0.1f } },
    };

    int Wrong = 0;
    for (const Case& C : Cases)
    {
        const Matrix M = LightClip(C.Tap, C.Centre);
        // Exactly the shader's expression, including its clamp.
        const float Element   = M.Columns[1][1];
        const float Recovered = 1.0f / (Element > 1.0e-6f ? Element : 1.0e-6f);

        // What that error does downstream: the penumbra radius is linear in 1/TanHalf via TexelsPerMetre, so the
        //    ratio below is the factor by which the soft edge is mis-sized.
        const float Ratio = Recovered / Truth;
        const bool  Ok    = std::fabs(Ratio - 1.0f) < 0.01f;
        if (!Ok) ++Wrong;
        std::printf("  %-34s %10.4f %10.4f %11.1fx   %s\n", C.Name,
                    static_cast<double>(Recovered), static_cast<double>(Truth),
                    static_cast<double>(Ratio), Ok ? "ok" : "WRONG");
    }

    std::printf("\n  %d of %zu light orientations recover the wrong angle.\n",
                Wrong, sizeof(Cases) / sizeof(Cases[0]));
    std::printf("  The matrix is Projection x View, so [1][1] carries the light's Up.y and not 1/tan(half).\n");
    std::printf("  A light aimed horizontally has Up.y = 0, the clamp floors it at 1e-6, and the recovered\n");
    std::printf("  half-angle explodes -- PCSS then reads a penumbra millions of times too wide.\n\n");
    const int Bad = ProjectionChecks();

    // Exit status reflects ONLY the projection checks. The table above documents a bug that has already been fixed
    //    — those six rows are SUPPOSED to read WRONG, because they show what the old matrix-recovery would still
    //    produce. Failing on them would make this program permanently red and therefore worthless as a gate.
    std::printf("\n  (the six WRONG rows are the fixed bug reproduced, not a regression)\n\n");
    (void)Wrong;
    return Bad == 0 ? 0 : 1;
}
