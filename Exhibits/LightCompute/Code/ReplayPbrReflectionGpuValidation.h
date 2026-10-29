/*==========================================================================================================================================
                                             REPLAYPBRREFLECTIONGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify compact VRAM byte-exactness, beta=2 power MIS variance reduction, and authored PBR + glazing reflections on Vulkan.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                      AUTHORED PBR MATERIAL ATLAS BUILDER
//------------------------------------------------------------------------------------------------------------------------

struct PbrShowroomMaterials
{
    uint32_t RacingGreenClearcoat = 0; // 2-stratum Clearcoat + 3-layer Voronoi metallic paint flakes
    uint32_t PolishedChromium = 0;     // Metallic = 1.0, Roughness = 0.07 + brushed-grain normal/albedo pyramids
    uint32_t MidnightVelvet = 0;       // Charlie fabric sheen + twill weave normal/albedo pyramids
    uint32_t GlazedSlateFloor = 0;     // 2-stratum glazed porcelain/slate showroom tile + grout normal/albedo pyramids
    uint32_t TintedGlazing = 0;        // Category == 2 thin-sheet tinted automotive/showroom glass (or opaque trim)
    uint32_t MatteArchitecture = 0;    // Architectural showroom wall
};

inline uint32_t AppendPbrMipPyramid(std::vector<float>& Atlas, uint32_t TopSize, int PatternKind, bool NormalMap)
{
    const uint32_t Start = static_cast<uint32_t>(Atlas.size());
    Atlas.insert(Atlas.end(), {float(TopSize), 0.0f, 0.0f, 0.0f});

    std::vector<float> Texels(std::size_t(TopSize) * TopSize * 3u, 0.0f);
    for (uint32_t Y = 0; Y < TopSize; ++Y)
    {
        for (uint32_t X = 0; X < TopSize; ++X)
        {
            const float U = (float(X) + 0.5f) / float(TopSize);
            const float V = (float(Y) + 0.5f) / float(TopSize);
            float R = 0.8f, G = 0.8f, B = 0.8f;
            if (!NormalMap)
            {
                if (PatternKind == 0)
                {
                    // Brushed chromium subtle anisotropic grain in [0.86, 1.0].
                    const float Grain = 0.93f + 0.07f * std::sin(V * 12.0f * float(std::numbers::pi));
                    R = 0.96f * Grain;
                    G = 0.98f * Grain;
                    B = 1.00f * Grain;
                }
                else if (PatternKind == 1)
                {
                    // Twill midnight-velvet weave modulation.
                    const float Diagonal = std::sin((U + V) * 8.0f * float(std::numbers::pi));
                    const float Weave = 0.82f + 0.18f * Diagonal;
                    R = 0.78f * Weave;
                    G = 0.86f * Weave;
                    B = 0.96f * Weave;
                }
                else
                {
                    // Showroom slate tile with crisp dark bevel grout lines.
                    const bool Grout = (X == 0u || Y == 0u || X + 1u == TopSize || Y + 1u == TopSize);
                    const float Checker = (((X / (TopSize / 2u)) + (Y / (TopSize / 2u))) & 1u) != 0u ? 0.94f : 0.78f;
                    const float Tone = Grout ? 0.34f : Checker;
                    R = 0.88f * Tone;
                    G = 0.92f * Tone;
                    B = 0.98f * Tone;
                }
            }
            else
            {
                float Nx = 0.0f, Ny = 0.0f, Nz = 1.0f;
                if (PatternKind == 0)
                {
                    // Fine brushed micro-grooves along V.
                    Ny = 0.055f * std::cos(V * 12.0f * float(std::numbers::pi));
                }
                else if (PatternKind == 1)
                {
                    // Diagonal twill fiber relief.
                    const float Slope = 0.14f * std::cos((U + V) * 8.0f * float(std::numbers::pi));
                    Nx = Slope;
                    Ny = -Slope;
                }
                else
                {
                    // Tile bevel normal around tile borders.
                    if (X == 0u) Nx = -0.18f;
                    else if (X + 1u == TopSize) Nx = 0.18f;
                    if (Y == 0u) Ny = -0.18f;
                    else if (Y + 1u == TopSize) Ny = 0.18f;
                }
                const float InvLen = 1.0f / std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
                R = Nx * InvLen;
                G = Ny * InvLen;
                B = Nz * InvLen;
            }
            const std::size_t Idx = (std::size_t(Y) * TopSize + X) * 3u;
            Texels[Idx + 0u] = R;
            Texels[Idx + 1u] = G;
            Texels[Idx + 2u] = B;
        }
    }

    uint32_t Width = TopSize;
    while (true)
    {
        Atlas.insert(Atlas.end(), Texels.begin(), Texels.end());
        if (Width == 1u)
        {
            break;
        }
        const uint32_t Next = Width / 2u;
        std::vector<float> Down(std::size_t(Next) * Next * 3u, 0.0f);
        for (uint32_t Y = 0; Y < Next; ++Y)
        {
            for (uint32_t X = 0; X < Next; ++X)
            {
                float Sum[3]{0.0f, 0.0f, 0.0f};
                for (uint32_t Dy = 0; Dy < 2u; ++Dy)
                {
                    for (uint32_t Dx = 0; Dx < 2u; ++Dx)
                    {
                        const std::size_t Src = (std::size_t(2u * Y + Dy) * Width + (2u * X + Dx)) * 3u;
                        for (int C = 0; C < 3; ++C) Sum[C] += 0.25f * Texels[Src + C];
                    }
                }
                if (NormalMap)
                {
                    const float Len = std::max(1e-6f, std::sqrt(Sum[0] * Sum[0] + Sum[1] * Sum[1] + Sum[2] * Sum[2]));
                    for (int C = 0; C < 3; ++C) Sum[C] /= Len;
                }
                const std::size_t Dst = (std::size_t(Y) * Next + X) * 3u;
                for (int C = 0; C < 3; ++C) Down[Dst + C] = Sum[C];
            }
        }
        Texels.swap(Down);
        Width = Next;
    }
    return Start;
}

inline PbrShowroomMaterials BuildPbrShowroomAtlas(std::vector<float>& Atlas, bool EnableThinSheetGlazing)
{
    Atlas.clear();
    Atlas.push_back(0.0f); // Address 0 means no authored material.

    auto AllocMaterial = [&]() -> uint32_t
    {
        const uint32_t Base = static_cast<uint32_t>(Atlas.size());
        Atlas.resize(Base + 268u, 0.0f);
        return Base;
    };

    PbrShowroomMaterials Mat{};

    // 1. Racing Green 2-Stratum Clearcoat + 3-Layer Voronoi Metallic Flake Paint (from racing-green.material).
    {
        const uint32_t Base = AllocMaterial();
        Atlas[Base + 0u] = 2.0f; // 2 strata
        Atlas[Base + 1u] = 0.0f; // Opaque multi-strata
        Atlas[Base + 4u] = 1.0f;
        Atlas[Base + 5u] = 1.0f;
        // Stratum 0: Metallic flake basecoat
        float* S0 = Atlas.data() + Base + 8u;
        S0[0] = 0.045f; S0[1] = 0.165f; S0[2] = 0.078f; S0[3] = 0.24f;
        S0[4] = 0.87f;  S0[5] = 1.50f;  S0[6] = 1.00f;
        // Stratum 1: Glossy Clearcoat (Classification == 1) with subtle emerald-amber Beer-Lambert depth
        float* S1 = Atlas.data() + Base + 8u + 24u;
        S1[0] = 1.00f;  S1[1] = 1.00f;  S1[2] = 1.00f;  S1[3] = 0.07f;
        S1[4] = 0.00f;  S1[5] = 1.52f;  S1[6] = 0.94f;
        S1[8] = 0.12f;  S1[9] = 0.02f;  S1[10] = 0.08f; S1[11] = 0.008f;
        S1[12] = 1.0f;
        // AutomotivePaint Voronoi cellular metallic flakes
        Atlas[Base + 200u] = 1.0f;
        Atlas[Base + 201u] = 3.0f;
        Atlas[Base + 202u] = 0.0f;
        Atlas[Base + 203u] = 5.0f;
        Atlas[Base + 204u] = 0.64f; Atlas[Base + 205u] = 450.0f; Atlas[Base + 206u] = 1.0f; Atlas[Base + 207u] = 0.65f;
        Atlas[Base + 208u] = 0.12f; Atlas[Base + 209u] = 0.30f;  Atlas[Base + 210u] = 0.85f; Atlas[Base + 211u] = 1.0f;
        Atlas[Base + 212u] = 0.18f; Atlas[Base + 213u] = 0.55f;  Atlas[Base + 214u] = 0.18f; Atlas[Base + 215u] = 24.0f;
        const float Stops[5][4] = {
            {0.565f, 0.708f, 0.250f, 0.00f},
            {0.100f, 0.515f, 0.366f, 0.25f},
            {0.407f, 0.258f, 0.584f, 0.50f},
            {0.638f, 0.198f, 0.227f, 0.75f},
            {0.223f, 0.366f, 0.665f, 1.00f},
        };
        for (uint32_t I = 0; I < 5u; ++I)
        {
            for (uint32_t C = 0; C < 4u; ++C) Atlas[Base + 216u + I * 4u + C] = Stops[I][C];
        }
        Atlas[Base + 264u] = 0.386f; Atlas[Base + 265u] = 0.409f; Atlas[Base + 266u] = 0.418f;
        Mat.RacingGreenClearcoat = Base + 1u;
    }

    // 2. Polished Chromium Conductor (from chromium.material, Metallic = 1.0, Roughness = 0.07 + pyramids).
    {
        const uint32_t Base = AllocMaterial();
        Atlas[Base + 0u] = 1.0f;
        Atlas[Base + 1u] = 0.0f;
        Atlas[Base + 4u] = 4.0f;
        Atlas[Base + 5u] = 2.0f;
        float* S0 = Atlas.data() + Base + 8u;
        S0[0] = 0.74f; S0[1] = 0.76f; S0[2] = 0.80f; S0[3] = 0.07f;
        S0[4] = 1.00f; S0[5] = 1.50f; S0[6] = 1.00f;
        const uint32_t Alb = AppendPbrMipPyramid(Atlas, 16u, 0, false);
        const uint32_t Nrm = AppendPbrMipPyramid(Atlas, 16u, 0, true);
        Atlas[Base + 2u] = float(Alb);
        Atlas[Base + 3u] = float(Nrm);
        Mat.PolishedChromium = Base + 1u;
    }

    // 3. Midnight Velvet Charlie-Sheen Fabric (from midnight-velvet.material).
    {
        const uint32_t Base = AllocMaterial();
        Atlas[Base + 0u] = 1.0f;
        Atlas[Base + 1u] = 0.0f;
        Atlas[Base + 4u] = 5.0f;
        Atlas[Base + 5u] = 5.0f;
        float* S0 = Atlas.data() + Base + 8u;
        S0[0] = 0.035f; S0[1] = 0.068f; S0[2] = 0.115f; S0[3] = 0.86f;
        S0[4] = 0.00f;  S0[5] = 1.50f;  S0[6] = 1.00f;
        S0[16] = 0.30f; S0[17] = 0.46f; S0[18] = 0.62f; S0[19] = 1.00f;
        S0[20] = 0.34f;
        const uint32_t Alb = AppendPbrMipPyramid(Atlas, 16u, 1, false);
        const uint32_t Nrm = AppendPbrMipPyramid(Atlas, 16u, 1, true);
        Atlas[Base + 2u] = float(Alb);
        Atlas[Base + 3u] = float(Nrm);
        Mat.MidnightVelvet = Base + 1u;
    }

    // 4. Showroom 2-Stratum Glazed Slate Tile Floor (base slate + glossy floor polish + tile grout pyramids).
    {
        const uint32_t Base = AllocMaterial();
        Atlas[Base + 0u] = 2.0f;
        Atlas[Base + 1u] = 0.0f;
        Atlas[Base + 4u] = 6.0f;
        Atlas[Base + 5u] = 6.0f;
        float* S0 = Atlas.data() + Base + 8u;
        S0[0] = 0.16f; S0[1] = 0.17f; S0[2] = 0.20f; S0[3] = 0.34f;
        S0[4] = 0.15f; S0[5] = 1.52f; S0[6] = 1.00f;
        float* S1 = Atlas.data() + Base + 8u + 24u;
        S1[0] = 1.00f; S1[1] = 1.00f; S1[2] = 1.00f; S1[3] = 0.11f;
        S1[4] = 0.00f; S1[5] = 1.49f; S1[6] = 0.85f;
        S1[12] = 1.0f;
        const uint32_t Alb = AppendPbrMipPyramid(Atlas, 16u, 2, false);
        const uint32_t Nrm = AppendPbrMipPyramid(Atlas, 16u, 2, true);
        Atlas[Base + 2u] = float(Alb);
        Atlas[Base + 3u] = float(Nrm);
        Mat.GlazedSlateFloor = Base + 1u;
    }

    // 5. Tinted Automotive / Showroom Glazing (Category == 2 thin-sheet glass or opaque control blocker).
    {
        const uint32_t Base = AllocMaterial();
        Atlas[Base + 0u] = 1.0f;
        Atlas[Base + 1u] = EnableThinSheetGlazing ? 2.0f : 0.0f;
        Atlas[Base + 4u] = 1.0f;
        Atlas[Base + 5u] = 1.0f;
        float* S0 = Atlas.data() + Base + 8u;
        S0[0] = EnableThinSheetGlazing ? 0.92f : 0.08f;
        S0[1] = EnableThinSheetGlazing ? 0.98f : 0.09f;
        S0[2] = EnableThinSheetGlazing ? 0.96f : 0.10f;
        S0[3] = 0.06f;
        S0[4] = 0.00f;
        S0[5] = 1.52f;
        S0[6] = 1.00f;
        S0[8] = 0.55f; S0[9] = 0.10f; S0[10] = 0.22f; S0[11] = 0.008f;
        Mat.TintedGlazing = Base + 1u;
    }

    // 6. Architectural Warm Matte Enclosure Wall.
    {
        const uint32_t Base = AllocMaterial();
        Atlas[Base + 0u] = 1.0f;
        Atlas[Base + 1u] = 0.0f;
        Atlas[Base + 4u] = 1.0f;
        Atlas[Base + 5u] = 1.0f;
        float* S0 = Atlas.data() + Base + 8u;
        S0[0] = 0.42f; S0[1] = 0.41f; S0[2] = 0.39f; S0[3] = 0.72f;
        S0[4] = 0.00f; S0[5] = 1.50f; S0[6] = 1.00f;
        Mat.MatteArchitecture = Base + 1u;
    }

    return Mat;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SHOWROOM GEOMETRY AND CAMERA BUILDER
//------------------------------------------------------------------------------------------------------------------------

inline void AppendPbrQuad(std::vector<LightComputeFacet>& Authored,
                          std::vector<std::array<SpatialVector, 3>>& Geometry,
                          SpatialVector P0, SpatialVector P1, SpatialVector P2, SpatialVector P3,
                          SpatialVector N0, SpatialVector N1, SpatialVector N2, SpatialVector N3,
                          uint32_t MaterialAddress, float Token,
                          std::array<float, 3> FallbackAlbedo = {0.5f, 0.5f, 0.5f},
                          float FallbackRoughness = 0.35f, float FallbackMetallic = 0.0f,
                          std::array<float, 3> Emissive = {0.0f, 0.0f, 0.0f})
{
    auto EmitTri = [&](SpatialVector A, SpatialVector B, SpatialVector C,
                       SpatialVector Na, SpatialVector Nb, SpatialVector Nc,
                       std::array<float, 2> UvA, std::array<float, 2> UvB, std::array<float, 2> UvC)
    {
        Geometry.push_back({A, B, C});
        LightComputeFacet F{};
        const bool HasMaterial = MaterialAddress > 0u;
        F.A = {float(A.X), float(A.Y), float(A.Z), HasMaterial ? UvA[0] : 0.0f};
        F.B = {float(B.X), float(B.Y), float(B.Z), HasMaterial ? UvB[0] : 0.0f};
        F.C = {float(C.X), float(C.Y), float(C.Z), HasMaterial ? UvC[0] : 0.0f};
        F.NormalA = {float(Na.X), float(Na.Y), float(Na.Z), HasMaterial ? UvA[1] : 0.0f};
        F.NormalB = {float(Nb.X), float(Nb.Y), float(Nb.Z), HasMaterial ? UvB[1] : 0.0f};
        F.NormalC = {float(Nc.X), float(Nc.Y), float(Nc.Z), HasMaterial ? UvC[1] : 0.0f};
        F.AlbedoRoughness = {FallbackAlbedo[0], FallbackAlbedo[1], FallbackAlbedo[2], FallbackRoughness};
        F.OpticalToken = {FallbackMetallic, 1.52f, 0.5f, Token};
        F.Emissive = {Emissive[0], Emissive[1], Emissive[2], float(MaterialAddress)};
        Authored.push_back(F);
    };
    EmitTri(P0, P1, P2, N0, N1, N2, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f});
    EmitTri(P0, P2, P3, N0, N2, N3, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
}

inline void SetShowroomCamera(LightComputeArchive& Scene,
                              std::array<float, 3> EyeCurrent,
                              std::array<float, 3> EyePrevious,
                              float YawCurrent = 0.0f,
                              float YawPrevious = 0.0f)
{
    auto MakeBasis = [](std::array<float, 3> Eye, float Yaw, std::array<float, 4>* Dest)
    {
        const float Pitch = -0.11f;
        const float CosP = std::cos(Pitch), SinP = std::sin(Pitch);
        const float CosY = std::cos(Yaw), SinY = std::sin(Yaw);
        // Orthonormal camera basis looking toward +Y with gentle yaw & downward pitch.
        const std::array<float, 3> Forward{SinY * CosP, CosY * CosP, SinP};
        const std::array<float, 3> Right{CosY, -SinY, 0.0f};
        const std::array<float, 3> Up{-SinY * SinP, -CosY * SinP, CosP};
        Dest[0] = {Eye[0], Eye[1], Eye[2], 0.08f};
        Dest[1] = {Forward[0], Forward[1], Forward[2], 60.0f};
        Dest[2] = {Right[0], Right[1], Right[2], 0.68f};
        Dest[3] = {Up[0], Up[1], Up[2], 0.68f};
    };
    Scene.Camera.fill({0.0f, 0.0f, 0.0f, 0.0f});
    MakeBasis(EyeCurrent, YawCurrent, &Scene.Camera[0]);
    MakeBasis(EyePrevious, YawPrevious, &Scene.Camera[4]);
    Scene.Camera[11] = {1.0f, 0.0f, 1.0f, 0.0f};
}

inline LightComputeArchive BuildPbrShowroomScene(uint32_t Width,
                                                 uint32_t Height,
                                                 bool EnableThinSheetGlazing,
                                                 std::array<float, 3> EyeCurrent = {0.0f, -1.65f, 1.55f},
                                                 std::array<float, 3> EyePrevious = {0.0f, -1.65f, 1.55f},
                                                 float YawCurrent = 0.0f,
                                                 float YawPrevious = 0.0f)
{
    LightComputeArchive Scene{};
    Scene.Width = Width;
    Scene.Height = Height;
    Scene.Receivers.resize(std::size_t(Width) * Height);

    const PbrShowroomMaterials Mat = BuildPbrShowroomAtlas(Scene.Angles, EnableThinSheetGlazing);

    std::vector<LightComputeFacet> Authored;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    Authored.reserve(160);
    Geometry.reserve(160);

    // 1. Showroom 2-stratum glazed slate floor (z = 0, token 1)
    AppendPbrQuad(Authored, Geometry,
                  {-4.8, -2.6, 0.0}, {4.8, -2.6, 0.0}, {4.8, 9.6, 0.0}, {-4.8, 9.6, 0.0},
                  {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
                  Mat.GlazedSlateFloor, 1.0f, {0.18f, 0.19f, 0.22f}, 0.28f, 0.15f);

    // 2. Architectural showroom ceiling & walls (2mm separated, tokens 2..5)
    AppendPbrQuad(Authored, Geometry,
                  {-4.8, 9.6, 4.4}, {4.8, 9.6, 4.4}, {4.8, -2.6, 4.4}, {-4.8, -2.6, 4.4},
                  {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
                  Mat.MatteArchitecture, 2.0f, {0.42f, 0.41f, 0.39f}, 0.72f, 0.0f);
    AppendPbrQuad(Authored, Geometry,
                  {-4.8, -2.6, 0.004}, {-4.8, 9.6, 0.004}, {-4.8, 9.6, 4.396}, {-4.8, -2.6, 4.396},
                  {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0},
                  Mat.MatteArchitecture, 3.0f, {0.42f, 0.41f, 0.39f}, 0.72f, 0.0f);
    AppendPbrQuad(Authored, Geometry,
                  {4.8, 9.6, 0.004}, {4.8, -2.6, 0.004}, {4.8, -2.6, 4.396}, {4.8, 9.6, 4.396},
                  {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
                  Mat.MatteArchitecture, 4.0f, {0.42f, 0.41f, 0.39f}, 0.72f, 0.0f);
    AppendPbrQuad(Authored, Geometry,
                  {-4.796, 9.596, 0.004}, {4.796, 9.596, 0.004}, {4.796, 9.596, 4.396}, {-4.796, 9.596, 4.396},
                  {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0},
                  Mat.MatteArchitecture, 5.0f, {0.36f, 0.36f, 0.38f}, 0.72f, 0.0f);

    // 3. Curved Racing-Green Clearcoat + Voronoi Metallic-Flake Coupé Hood Shell (12 smooth strips, token 10)
    constexpr int HoodSegments = 12;
    for (int Seg = 0; Seg < HoodSegments; ++Seg)
    {
        const double T0 = double(Seg) / double(HoodSegments);
        const double T1 = double(Seg + 1) / double(HoodSegments);
        auto HoodProfile = [](double T, double& Y, double& Z, SpatialVector& Nrm)
        {
            const double Angle = -0.58 + 1.16 * T;
            Y = 2.85 + 1.55 * T;
            Z = 0.18 + 0.78 * std::cos(Angle * 0.85) - 0.14 * (1.0 - T);
            const double DzDt = -0.78 * 0.85 * 1.16 * std::sin(Angle * 0.85) + 0.14;
            const double DyDt = 1.55;
            const double Len = std::sqrt(DzDt * DzDt + DyDt * DyDt);
            Nrm = {0.0, -DzDt / Len, DyDt / Len};
        };
        double Y0 = 0.0, Z0 = 0.0, Y1 = 0.0, Z1 = 0.0;
        SpatialVector N0{}, N1{};
        HoodProfile(T0, Y0, Z0, N0);
        HoodProfile(T1, Y1, Z1, N1);
        // Gentle lateral crown across X in [-1.55, -0.05] for realistic 3D clearcoat curvature!
        for (int Lat = 0; Lat < 4; ++Lat)
        {
            const double U0 = double(Lat) / 4.0, U1 = double(Lat + 1) / 4.0;
            const double X0 = -1.62 + 1.58 * U0, X1 = -1.62 + 1.58 * U1;
            const double Crown0 = 0.14 * std::sin(U0 * std::numbers::pi);
            const double Crown1 = 0.14 * std::sin(U1 * std::numbers::pi);
            const double Nx0 = -0.22 * std::cos(U0 * std::numbers::pi);
            const double Nx1 = -0.22 * std::cos(U1 * std::numbers::pi);
            auto CombineN = [](SpatialVector Base, double LateralX) -> SpatialVector
            {
                const double L = std::sqrt(LateralX * LateralX + Base.Y * Base.Y + Base.Z * Base.Z);
                return {LateralX / L, Base.Y / L, Base.Z / L};
            };
            AppendPbrQuad(Authored, Geometry,
                          {X0, Y0, Z0 + Crown0}, {X1, Y0, Z0 + Crown1},
                          {X1, Y1, Z1 + Crown1}, {X0, Y1, Z1 + Crown0},
                          CombineN(N0, Nx0), CombineN(N0, Nx1), CombineN(N1, Nx1), CombineN(N1, Nx0),
                          Mat.RacingGreenClearcoat, 10.0f, {0.05f, 0.17f, 0.08f}, 0.24f, 0.87f);
        }
    }

    // 4. Curved Polished Chromium Mirror Column (Roughness = 0.07, Metallic = 1.0, token 20)
    constexpr int ChromeSlices = 10;
    for (int Slice = 0; Slice < ChromeSlices; ++Slice)
    {
        const double A0 = std::numbers::pi * (1.05 + 0.90 * double(Slice) / double(ChromeSlices));
        const double A1 = std::numbers::pi * (1.05 + 0.90 * double(Slice + 1) / double(ChromeSlices));
        const double Cx = 0.55, Cy = 4.55, Radius = 0.58;
        const SpatialVector N0{std::cos(A0), std::sin(A0), 0.0};
        const SpatialVector N1{std::cos(A1), std::sin(A1), 0.0};
        const SpatialVector P0{Cx + Radius * N0.X, Cy + Radius * N0.Y, 0.01};
        const SpatialVector P1{Cx + Radius * N1.X, Cy + Radius * N1.Y, 0.01};
        const SpatialVector P2{Cx + Radius * N1.X, Cy + Radius * N1.Y, 2.75};
        const SpatialVector P3{Cx + Radius * N0.X, Cy + Radius * N0.Y, 2.75};
        AppendPbrQuad(Authored, Geometry, P0, P1, P2, P3, N0, N1, N1, N0,
                      Mat.PolishedChromium, 20.0f, {0.74f, 0.76f, 0.80f}, 0.07f, 1.0f);
    }

    // 5. Curved Midnight-Velvet Charlie-Sheen Drape Backdrop (left-centre, token 30)
    for (int Fold = 0; Fold < 6; ++Fold)
    {
        const double U0 = double(Fold) / 6.0, U1 = double(Fold + 1) / 6.0;
        const double X0 = -3.35 + 1.45 * U0, X1 = -3.35 + 1.45 * U1;
        const double Y0 = 5.40 + 0.22 * std::sin(U0 * 3.0 * std::numbers::pi);
        const double Y1 = 5.40 + 0.22 * std::sin(U1 * 3.0 * std::numbers::pi);
        const double Slope0 = 0.22 * 3.0 * std::numbers::pi / 1.45 * std::cos(U0 * 3.0 * std::numbers::pi);
        const double Slope1 = 0.22 * 3.0 * std::numbers::pi / 1.45 * std::cos(U1 * 3.0 * std::numbers::pi);
        auto DrapeNormal = [](double Slope) -> SpatialVector
        {
            const double L = std::sqrt(Slope * Slope + 1.0);
            return {Slope / L, -1.0 / L, 0.0};
        };
        const SpatialVector N0 = DrapeNormal(Slope0), N1 = DrapeNormal(Slope1);
        AppendPbrQuad(Authored, Geometry,
                      {X0, Y0, 0.01}, {X1, Y1, 0.01}, {X1, Y1 + 0.15, 2.65}, {X0, Y0 + 0.15, 2.65},
                      N0, N1, N1, N0, Mat.MidnightVelvet, 30.0f, {0.04f, 0.07f, 0.12f}, 0.86f, 0.0f);
    }

    // 6. Right-Side Tinted Automotive/Showroom Glazing Partition (Category == 2 thin-sheet glass, token 40)
    //    Positioned in front of a bright warm-amber display wall + cyan accent strip so pixels see BOTH
    //    transmitted Beer-Lambert showroom graphics AND grazing Fresnel reflections!
    AppendPbrQuad(Authored, Geometry,
                  {1.35, 3.35, 0.02}, {2.95, 3.65, 0.02}, {2.95, 3.65, 2.35}, {1.35, 3.35, 2.35},
                  {-0.184, -0.983, 0.0}, {-0.184, -0.983, 0.0}, {-0.184, -0.983, 0.0}, {-0.184, -0.983, 0.0},
                  Mat.TintedGlazing, 40.0f, {0.92f, 0.98f, 0.96f}, 0.06f, 0.0f);

    // 7. Showroom Architectural Area Luminaires (overhead softbox + warm emblem behind glass + gold/cyan wall bars)
    // Overhead wide softbox diffuser (token 50)
    AppendPbrQuad(Authored, Geometry,
                  {-2.45, 2.1, 4.36}, {2.45, 2.1, 4.36}, {2.45, 6.7, 4.36}, {-2.45, 6.7, 4.36},
                  {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
                  0u, 50.0f, {0.0f, 0.0f, 0.0f}, 0.5f, 0.0f, {6.8f, 6.6f, 6.2f});
    // Warm amber showroom display panel behind the tinted glass partition (token 51)
    AppendPbrQuad(Authored, Geometry,
                  {1.55, 6.20, 0.45}, {2.85, 6.20, 0.45}, {2.85, 6.20, 2.05}, {1.55, 6.20, 2.05},
                  {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0},
                  0u, 51.0f, {0.0f, 0.0f, 0.0f}, 0.5f, 0.0f, {8.8f, 4.4f, 1.2f});
    // Left warm-gold vertical accent light bar (token 52)
    AppendPbrQuad(Authored, Geometry,
                  {-4.75, 2.6, 0.55}, {-4.75, 3.5, 0.55}, {-4.75, 3.5, 3.25}, {-4.75, 2.6, 3.25},
                  {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0},
                  0u, 52.0f, {0.0f, 0.0f, 0.0f}, 0.5f, 0.0f, {6.4f, 3.4f, 1.0f});
    // Right cool-cyan vertical accent light bar (token 53)
    AppendPbrQuad(Authored, Geometry,
                  {4.75, 4.8, 0.55}, {4.75, 3.9, 0.55}, {4.75, 3.9, 3.25}, {4.75, 4.8, 3.25},
                  {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
                  0u, 53.0f, {0.0f, 0.0f, 0.0f}, 0.5f, 0.0f, {1.1f, 5.2f, 8.2f});

    TriangleVisibilitySpace Space;
    Space.BuildSurfaceAreaHierarchy(Geometry, 2u);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.resize(Scene.Visibility.OriginalFacets.size());
    for (std::size_t I = 0u; I < Scene.Visibility.OriginalFacets.size(); ++I)
    {
        Scene.Surfaces[I] = Authored[Scene.Visibility.OriginalFacets[I]];
    }

    SetShowroomCamera(Scene, EyeCurrent, EyePrevious, YawCurrent, YawPrevious);
    return Scene;
}

//------------------------------------------------------------------------------------------------------------------------
//                           GPU BENCHMARK: COMPACT VRAM + POWER MIS + PBR SHOWCASE
//------------------------------------------------------------------------------------------------------------------------

inline double ComputePbrRmse(const std::vector<std::array<float, 4>>& Estimate,
                             const std::vector<std::array<float, 4>>& Reference,
                             const std::vector<LightComputeReceiver>* MaskReceivers = nullptr,
                             float RequiredToken = -1.0f)
{
    Require(Estimate.size() == Reference.size() && (!MaskReceivers || MaskReceivers->size() == Estimate.size()),
            "PBR comparison extents do not match");
    double SumSq = 0.0;
    std::size_t Count = 0u;
    for (std::size_t I = 0; I < Estimate.size(); ++I)
    {
        if (MaskReceivers && RequiredToken > 0.0f)
        {
            if ((*MaskReceivers)[I].PositionHit[3] == 0.0f ||
                std::abs((*MaskReceivers)[I].MotionPositionToken[3] - RequiredToken) > 0.1f)
            {
                continue;
            }
        }
        for (uint32_t C = 0; C < 3u; ++C)
        {
            const double Diff = double(Estimate[I][C]) - double(Reference[I][C]);
            SumSq += Diff * Diff;
        }
        Count += 3u;
    }
    Require(Count > 0u, "PBR comparison mask selected no pixels");
    return std::sqrt(SumSq / double(Count));
}

// Builds a 64x64 1-bounce glossy glazed-slate + matte sill reflection bench right beneath a broad
// close overhead light canopy (z = 0.085 m, Veach close-emitter 1/R^2 Jacobian MIS regime).
inline LightComputeArchive BuildGlossyCanopyMisScene(uint32_t Width, uint32_t Height)
{
    LightComputeArchive Scene{};
    Scene.Width = Width;
    Scene.Height = Height;
    Scene.Receivers.resize(std::size_t(Width) * Height);

    const PbrShowroomMaterials Mat = BuildPbrShowroomAtlas(Scene.Angles, true);

    std::vector<LightComputeFacet> Authored;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    // Left half: 2-stratum Glazed Slate automotive sill panel (token 10)
    AppendPbrQuad(Authored, Geometry,
                  {-4.2, -0.2, 0.0}, {0.0, -0.2, 0.0}, {0.0, 6.8, 0.0}, {-4.2, 6.8, 0.0},
                  {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
                  Mat.GlazedSlateFloor, 10.0f, {0.18f, 0.19f, 0.22f}, 0.28f, 0.15f);
    // Right half: Matte Architecture diffuse trim panel (token 20)
    AppendPbrQuad(Authored, Geometry,
                  {0.0, -0.2, 0.0}, {4.2, -0.2, 0.0}, {4.2, 6.8, 0.0}, {0.0, 6.8, 0.0},
                  {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
                  Mat.MatteArchitecture, 20.0f, {0.42f, 0.41f, 0.39f}, 0.72f, 0.0f);
    // Broad close overhead showroom canopy emitter at z = 0.085m (token 50)
    AppendPbrQuad(Authored, Geometry,
                  {-4.6, -0.8, 0.085}, {4.6, -0.8, 0.085}, {4.6, 7.2, 0.085}, {-4.6, 7.2, 0.085},
                  {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
                  0u, 50.0f, {0.0f, 0.0f, 0.0f}, 0.5f, 0.0f, {1.2f, 1.2f, 1.2f});

    TriangleVisibilitySpace Space;
    Space.BuildSurfaceAreaHierarchy(Geometry, 2u);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.resize(Scene.Visibility.OriginalFacets.size());
    for (std::size_t I = 0u; I < Scene.Visibility.OriginalFacets.size(); ++I)
    {
        Scene.Surfaces[I] = Authored[Scene.Visibility.OriginalFacets[I]];
    }

    const float Pitch = -0.18f;
    const float CosP = std::cos(Pitch), SinP = std::sin(Pitch);
    Scene.Camera.fill({0.0f, 0.0f, 0.0f, 0.0f});
    for (uint32_t Base : {0u, 4u})
    {
        Scene.Camera[Base + 0u] = {0.0f, -0.05f, 0.062f, 0.01f};
        Scene.Camera[Base + 1u] = {0.0f, CosP, SinP, 20.0f};
        Scene.Camera[Base + 2u] = {1.0f, 0.0f, 0.0f, 0.48f};
        Scene.Camera[Base + 3u] = {0.0f, -SinP, CosP, 0.48f};
    }
    Scene.Camera[11] = {1.0f, 0.0f, 1.0f, 0.0f};
    return Scene;
}

inline void ValidateReplayPbrReflectionAll(const char* Driver, const std::string& Shaders,
                                           const std::string& CaptureDir = "")
{
    constexpr uint32_t Width = 64u, Height = 64u;
    const auto GlazedBase = BuildPbrShowroomScene(Width, Height, true);
    const auto OpaqueControl = BuildPbrShowroomScene(Width, Height, false);
    Require(GlazedBase.Surfaces.size() == OpaqueControl.Surfaces.size() &&
                GlazedBase.Angles.size() == OpaqueControl.Angles.size(),
            "Glazed and opaque control showroom scenes must share identical buffer extents");

    // Single LightComputeSubmission compiles Subzero pipelines once (~1.1 GB RSS), then rebinds Buffers[6]
    // from the Legacy 46*N+15425 buffer to a genuine Compact 17*N VkBuffer + VkDeviceMemory.
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(GlazedBase, Shaders, false, false, false, false, true, false, false);

    const uint64_t LegacyBufBytes = Compute.OutputBufferBytes();
    const uint64_t LegacyAllocBytes = Compute.OutputAllocationBytes();
    const uint64_t LegacyTotalBytes = Compute.TotalAllocatedBytes();

    const std::array<uint32_t, 12> ComparedSlots{0, 1, 2, 3, 4, 9, 15, 16, 17, 18, 19, 20};
    auto MakeCompactTestScene = [&](uint32_t Frame)
    {
        const float TCurr = float(Frame) * 0.055f;
        const float TPrev = Frame == 0u ? 0.0f : float(Frame - 1u) * 0.055f;
        return BuildPbrShowroomScene(
            Width, Height, true,
            {-0.18f + TCurr, -1.65f + 0.35f * TCurr, 1.55f},
            {-0.18f + TPrev, -1.65f + 0.35f * TPrev, 1.55f},
            0.03f * float(Frame), Frame == 0u ? 0.0f : 0.03f * float(Frame - 1u));
    };

    // ---------------------------------------------------------------------------------------------------------
    // 1A. CAPTURE LEGACY 46*N+15425 OUTPUT, COUNTERS, AND ALL 12 LOGICAL SLABS ACROSS 4 MOVING FRAMES
    // ---------------------------------------------------------------------------------------------------------
    std::array<std::vector<std::array<float, 4>>, 4> LegacyFrames;
    std::array<std::array<uint32_t, 24>, 4> LegacyStats;
    std::array<std::array<std::vector<std::array<float, 4>>, 12>, 4> LegacySlabs;
    for (uint32_t Frame = 0u; Frame < 4u; ++Frame)
    {
        const auto FrameScene = MakeCompactTestScene(Frame);
        const uint32_t Seed = 41001u + Frame * 977u;
        LegacyFrames[Frame] = Compute.ExecuteReplay(
            FrameScene, 3u, Seed, true, true, Frame == 0u, {0.025f, 0.030f, 0.040f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 1u, false, false, false, true, true, true, true, false, true);
        LegacyStats[Frame] = Compute.ExtendedStatistics();
        for (std::size_t S = 0u; S < ComparedSlots.size(); ++S)
        {
            LegacySlabs[Frame][S] = Compute.ReadReplayLogicalSlab(ComparedSlots[S], false);
        }
    }

    // Reallocate Buffers[6] as a true 17*N Compact VkBuffer + VkDeviceMemory.
    Compute.RebindOutputStorage(true, false, true);
    const uint64_t CompactBufBytes = Compute.OutputBufferBytes();
    const uint64_t CompactAllocBytes = Compute.OutputAllocationBytes();
    const uint64_t CompactTotalBytes = Compute.TotalAllocatedBytes();
    const double BufferReductionPct = 100.0 * (1.0 - double(CompactBufBytes) / double(LegacyBufBytes));
    const double TotalReductionPct = 100.0 * (1.0 - double(CompactTotalBytes) / double(LegacyTotalBytes));
    Require(CompactBufBytes == uint64_t(Width) * Height * PathReplayCompactVarianceFilteredSlabs * 16ull &&
                BufferReductionPct > 65.0,
            "Compact Vulkan buffer did not shrink to 17*N float4 records");

    // ---------------------------------------------------------------------------------------------------------
    // 1B. VERIFY BYTE-EXACT COMPACT VRAM PARITY ACROSS ALL 4 FRAMES, 12 SLABS, AND 24 COUNTERS
    // ---------------------------------------------------------------------------------------------------------
    for (uint32_t Frame = 0u; Frame < 4u; ++Frame)
    {
        const auto FrameScene = MakeCompactTestScene(Frame);
        const uint32_t Seed = 41001u + Frame * 977u;
        const auto CompactImg = Compute.ExecuteReplay(
            FrameScene, 3u, Seed, true, true, Frame == 0u, {0.025f, 0.030f, 0.040f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 1u, false, false, false, true, true, true, true, true, true);

        Require(LegacyFrames[Frame] == CompactImg, "Compact VRAM output RGBA differed from legacy layout");
        Require(LegacyStats[Frame] == Compute.ExtendedStatistics(),
                "Compact VRAM execution counters differed from legacy layout");
        for (std::size_t S = 0u; S < ComparedSlots.size(); ++S)
        {
            const auto CmpSlab = Compute.ReadReplayLogicalSlab(ComparedSlots[S], true);
            Require(LegacySlabs[Frame][S] == CmpSlab,
                    "Compact VRAM logical history/SVGF slab differed from legacy layout");
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // 2. THIN-SHEET TINTED GLAZING PHYSICAL TRANSMITTANCE + FRESNEL REFLECTION VERIFICATION
    // ---------------------------------------------------------------------------------------------------------
    double GlazedWindowLum = 0.0, BlockedWindowLum = 0.0;
    std::size_t WindowPixels = 0u;
    {
        const auto GlazedView = Compute.ExecuteReplay(
            GlazedBase, 3u, 77101u, false, false, true, {0.02f, 0.025f, 0.035f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 4u, false, false, false, false, false, true, true, true, true);
        const auto Receivers = Compute.PrimaryReceivers();
        const auto BlockedView = Compute.ExecuteReplay(
            OpaqueControl, 3u, 77101u, false, false, true, {0.02f, 0.025f, 0.035f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 4u, false, false, false, false, false, true, true, true, true);

        for (std::size_t I = 0; I < Receivers.size(); ++I)
        {
            if (Receivers[I].PositionHit[3] != 0.0f && std::abs(Receivers[I].MotionPositionToken[3] - 40.0f) < 0.1f)
            {
                GlazedWindowLum += 0.2126 * GlazedView[I][0] + 0.7152 * GlazedView[I][1] + 0.0722 * GlazedView[I][2];
                BlockedWindowLum += 0.2126 * BlockedView[I][0] + 0.7152 * BlockedView[I][1] + 0.0722 * BlockedView[I][2];
                ++WindowPixels;
            }
        }
        Require(WindowPixels >= 40u && GlazedWindowLum > BlockedWindowLum * 2.2,
                "Thin-sheet glazing failed to transmit showroom display light behind glass partition");
    }

    // ---------------------------------------------------------------------------------------------------------
    // 3. 6-FRAME MOVING SHOWROOM BENCHMARK & CONVERGED 256-SPP REFERENCE (BALANCE vs POWER MIS + FULL PIPELINE)
    // ---------------------------------------------------------------------------------------------------------
    constexpr uint32_t MotionFrames = 6u;
    auto MakeFrameScene = [&](uint32_t Frame)
    {
        const float TCurr = float(Frame) * 0.06f;
        const float TPrev = Frame == 0u ? 0.0f : float(Frame - 1u) * 0.06f;
        return BuildPbrShowroomScene(
            Width, Height, true,
            {-0.22f + TCurr, -1.72f + 0.30f * TCurr, 1.54f},
            {-0.22f + TPrev, -1.72f + 0.30f * TPrev, 1.54f},
            0.028f * float(Frame), Frame == 0u ? 0.0f : 0.028f * float(Frame - 1u));
    };

    // ---------------------------------------------------------------------------------------------------------
    // 2B. GLOSSY NEAR-CANOPY REFLECTION BENCHMARK: BETA=2 POWER MIS VS BETA=1 BALANCE MIS (16 PAIRED BATCHES)
    // ---------------------------------------------------------------------------------------------------------
    auto CanopyScene = BuildGlossyCanopyMisScene(Width, Height);
    constexpr uint32_t CanopyRefPasses = 64u;
    std::vector<std::array<float, 4>> CanopyRef(std::size_t(Width) * Height, {0.0f, 0.0f, 0.0f, 1.0f});
    for (uint32_t Pass = 0u; Pass < CanopyRefPasses; ++Pass)
    {
        const bool TracePrimary = (Pass == 0u);
        const bool UsePowerRef = (Pass & 1u) != 0u;
        const auto Img = Compute.ExecuteReplay(
            CanopyScene, 1u, 810001u + Pass * 6151u, false, false, true, {0.020f, 0.020f, 0.020f}, 0.01f,
            TracePrimary, false, true, true, false, false, 64u, true, false, true, false, false,
            false, false, false, 1u, false, false, false, false, false, true, UsePowerRef, true, true);
        if (TracePrimary)
        {
            CanopyScene.Receivers = Compute.PrimaryReceivers();
        }
        for (std::size_t I = 0; I < CanopyRef.size(); ++I)
        {
            for (uint32_t C = 0; C < 3u; ++C)
            {
                CanopyRef[I][C] += Img[I][C] / float(CanopyRefPasses);
            }
        }
    }
    constexpr uint32_t CanopyBatches = 16u;
    double CanopyBalRmseSum = 0.0, CanopyPowRmseSum = 0.0;
    uint64_t CanopyBalRays = 0u, CanopyPowRays = 0u;
    for (uint32_t Batch = 0u; Batch < CanopyBatches; ++Batch)
    {
        const uint32_t Seed = 850001u + Batch * 4099u;
        const auto Bal = Compute.ExecuteReplay(
            CanopyScene, 1u, Seed, false, false, true, {0.020f, 0.020f, 0.020f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, false, false,
            false, false, false, 1u, false, false, false, false, false, true, false, true, true);
        const auto BalStats = Compute.ExtendedStatistics();
        CanopyBalRays += uint64_t(BalStats[2]) + BalStats[8] + BalStats[10] + BalStats[11];

        const auto Pow = Compute.ExecuteReplay(
            CanopyScene, 1u, Seed, false, false, true, {0.020f, 0.020f, 0.020f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, false, false,
            false, false, false, 1u, false, false, false, false, false, true, true, true, true);
        const auto PowStats = Compute.ExtendedStatistics();
        CanopyPowRays += uint64_t(PowStats[2]) + PowStats[8] + PowStats[10] + PowStats[11];

        CanopyBalRmseSum += ComputePbrRmse(Bal, CanopyRef);
        CanopyPowRmseSum += ComputePbrRmse(Pow, CanopyRef);
    }
    Require(CanopyBalRays == CanopyPowRays, "Power MIS changed ray count on glossy canopy reflection bench");
    const double CanopyBalanceRmse = CanopyBalRmseSum / double(CanopyBatches);
    const double CanopyPowerRmse = CanopyPowRmseSum / double(CanopyBatches);

    // ---------------------------------------------------------------------------------------------------------
    // 3. 3-BOUNCE AUTHORED PBR SHOWROOM: 80-SAMPLE SAME-SHADER ESTIMATE + MOVING-CAMERA COMPARISON
    // ---------------------------------------------------------------------------------------------------------
    auto FinalScene = MakeFrameScene(MotionFrames - 1u);

    constexpr uint32_t RefPasses = 80u;
    std::vector<std::array<float, 4>> Reference(std::size_t(Width) * Height, {0.0f, 0.0f, 0.0f, 1.0f});
    std::vector<LightComputeReceiver> FinalReceivers;
    for (uint32_t Pass = 0u; Pass < RefPasses; ++Pass)
    {
        const bool TracePrimary = (Pass == 0u);
        const bool UsePowerRef = (Pass & 1u) != 0u;
        const auto Img = Compute.ExecuteReplay(
            FinalScene, 3u, 900001u + Pass * 7919u, false, false, true, {0.025f, 0.030f, 0.040f}, 0.01f,
            TracePrimary, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 1u, false, false, false, false, false, true, UsePowerRef, true, true);
        if (TracePrimary)
        {
            FinalReceivers = Compute.PrimaryReceivers();
            FinalScene.Receivers = FinalReceivers;
        }
        for (std::size_t I = 0; I < Reference.size(); ++I)
        {
            for (uint32_t C = 0; C < 3u; ++C)
            {
                Reference[I][C] += Img[I][C] / float(RefPasses);
            }
        }
    }

    // Paired 8-batch check on closed 3-bounce PBR showroom: Balance MIS vs Power MIS (exact equal ray counts).
    constexpr uint32_t PairedBatches = 8u;
    double BalanceFullRmseSum = 0.0, PowerFullRmseSum = 0.0;
    double BalanceSpecRmseSum = 0.0, PowerSpecRmseSum = 0.0;
    uint64_t BalanceRays = 0u, PowerRays = 0u;
    std::vector<std::array<float, 4>> FirstBalanceRaw, FirstPowerRaw;
    for (uint32_t Batch = 0u; Batch < PairedBatches; ++Batch)
    {
        const uint32_t Seed = 510001u + Batch * 4099u;
        const auto Bal = Compute.ExecuteReplay(
            FinalScene, 3u, Seed, false, false, true, {0.025f, 0.030f, 0.040f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 1u, false, false, false, false, false, true, false, true, true);
        const auto BalStats = Compute.ExtendedStatistics();
        BalanceRays += uint64_t(BalStats[2]) + BalStats[8] + BalStats[10] + BalStats[11];

        const auto Pow = Compute.ExecuteReplay(
            FinalScene, 3u, Seed, false, false, true, {0.025f, 0.030f, 0.040f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 1u, false, false, false, false, false, true, true, true, true);
        const auto PowStats = Compute.ExtendedStatistics();
        PowerRays += uint64_t(PowStats[2]) + PowStats[8] + PowStats[10] + PowStats[11];

        BalanceFullRmseSum += ComputePbrRmse(Bal, Reference);
        PowerFullRmseSum += ComputePbrRmse(Pow, Reference);
        const double BalChrome = ComputePbrRmse(Bal, Reference, &FinalReceivers, 20.0f);
        const double PowChrome = ComputePbrRmse(Pow, Reference, &FinalReceivers, 20.0f);
        const double BalHood = ComputePbrRmse(Bal, Reference, &FinalReceivers, 10.0f);
        const double PowHood = ComputePbrRmse(Pow, Reference, &FinalReceivers, 10.0f);
        BalanceSpecRmseSum += 0.5 * (BalChrome + BalHood);
        PowerSpecRmseSum += 0.5 * (PowChrome + PowHood);
        if (Batch == 0u)
        {
            FirstBalanceRaw = Bal;
            FirstPowerRaw = Pow;
        }
    }
    Require(BalanceRays == PowerRays, "Power MIS changed ray count relative to Balance MIS");
    const double MeanBalanceRmse = BalanceFullRmseSum / double(PairedBatches);
    const double MeanPowerRmse = PowerFullRmseSum / double(PairedBatches);
    const double MeanBalanceSpecRmse = BalanceSpecRmseSum / double(PairedBatches);
    const double MeanPowerSpecRmse = PowerSpecRmseSum / double(PairedBatches);

    // Independent stationary-pose raw 1-spp pass at Seed+131 for pure Monte Carlo temporal flicker measurement.
    const std::vector<std::array<float, 4>> RawJitterNext = Compute.ExecuteReplay(
        FinalScene, 3u, 510001u + MotionFrames * 131u, false, false, true,
        {0.025f, 0.030f, 0.040f}, 0.01f, true, false, true, true, false, false, 64u, true, false, true,
        true, true, false, false, false, 1u, false, false, false, false, false, true, true, true, true);

    // 6-frame moving-camera Unified ReSTIR GI + Vogel Disk (No SVGF) vs Full Unified + Compact VRAM + Specular-Cone SVGF,
    // followed by 1 stationary settling frame at the final camera pose (`PreviousCamera == Camera`) to measure pure
    // consecutive-frame temporal seed-jitter flicker without geometric camera parallax.
    std::vector<std::array<float, 4>> UnifiedNoSvgf, UnifiedFullSvgf, UnifiedFullSvgfNext;
    for (uint32_t Frame = 0u; Frame < MotionFrames; ++Frame)
    {
        const auto SceneF = MakeFrameScene(Frame);
        const uint32_t Seed = 510001u + Frame * 131u;
        const bool Reset = (Frame == 0u);
        UnifiedNoSvgf = Compute.ExecuteReplay(
            SceneF, 3u, Seed, true, true, Reset, {0.025f, 0.030f, 0.040f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 1u, false, false, false, true, false, true, true, true, true);
    }
    for (uint32_t Frame = 0u; Frame < MotionFrames; ++Frame)
    {
        const auto SceneF = MakeFrameScene(Frame);
        const uint32_t Seed = 510001u + Frame * 131u;
        const bool Reset = (Frame == 0u);
        UnifiedFullSvgf = Compute.ExecuteReplay(
            SceneF, 3u, Seed, true, true, Reset, {0.025f, 0.030f, 0.040f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 1u, false, false, false, true, true, true, true, true, true);
    }
    {
        auto StaticFinal = MakeFrameScene(MotionFrames - 1u);
        for (uint32_t K = 0u; K < 4u; ++K)
        {
            StaticFinal.Camera[4u + K] = StaticFinal.Camera[K];
        }
        UnifiedFullSvgfNext = Compute.ExecuteReplay(
            StaticFinal, 3u, 510001u + MotionFrames * 131u, true, true, false, {0.025f, 0.030f, 0.040f}, 0.01f,
            true, false, true, true, false, false, 64u, true, false, true, true, true,
            false, false, false, 1u, false, false, false, true, true, true, true, true, true);
    }

    const double RawBalanceFrameRmse = ComputePbrRmse(FirstBalanceRaw, Reference);
    const double RawPowerFrameRmse = ComputePbrRmse(FirstPowerRaw, Reference);
    const double UnifiedNoSvgfRmse = ComputePbrRmse(UnifiedNoSvgf, Reference);
    const double UnifiedFullSvgfRmse = ComputePbrRmse(UnifiedFullSvgf, Reference);
    const double RawFlicker = ComputePbrRmse(FirstPowerRaw, RawJitterNext);
    const double FullSvgfFlicker = ComputePbrRmse(UnifiedFullSvgf, UnifiedFullSvgfNext);

    const double HoodRmse = ComputePbrRmse(UnifiedFullSvgf, Reference, &FinalReceivers, 10.0f);
    const double ChromeRmse = ComputePbrRmse(UnifiedFullSvgf, Reference, &FinalReceivers, 20.0f);
    const double VelvetRmse = ComputePbrRmse(UnifiedFullSvgf, Reference, &FinalReceivers, 30.0f);
    const double GlazingRmse = ComputePbrRmse(UnifiedFullSvgf, Reference, &FinalReceivers, 40.0f);

    if (!CaptureDir.empty())
    {
        std::filesystem::create_directories(CaptureDir);
        WriteImage(CaptureDir + "/ShowroomBalanceRaw.ppm", FirstBalanceRaw, Width, Height, 0.52);
        WriteImage(CaptureDir + "/ShowroomPowerRaw.ppm", FirstPowerRaw, Width, Height, 0.52);
        WriteImage(CaptureDir + "/ShowroomUnifiedNoSvgf.ppm", UnifiedNoSvgf, Width, Height, 0.52);
        WriteImage(CaptureDir + "/ShowroomUnifiedFullSvgf.ppm", UnifiedFullSvgf, Width, Height, 0.52);
        WriteImage(CaptureDir + "/ShowroomReference.ppm", Reference, Width, Height, 0.52);

        auto WriteLinearFloatRgb = [&](const std::string& Path, const std::vector<std::array<float, 4>>& Img)
        {
            std::ofstream Out(Path, std::ios::binary);
            Require(bool(Out), "Failed to open linear float RGB dump");
            for (const auto& P : Img)
            {
                Out.write(reinterpret_cast<const char*>(P.data()), 3 * sizeof(float));
            }
        };
        WriteLinearFloatRgb(CaptureDir + "/ShowroomBalanceRaw.f32", FirstBalanceRaw);
        WriteLinearFloatRgb(CaptureDir + "/ShowroomPowerRaw.f32", FirstPowerRaw);
        WriteLinearFloatRgb(CaptureDir + "/ShowroomUnifiedNoSvgf.f32", UnifiedNoSvgf);
        WriteLinearFloatRgb(CaptureDir + "/ShowroomUnifiedFullSvgf.f32", UnifiedFullSvgf);
        WriteLinearFloatRgb(CaptureDir + "/ShowroomReference.f32", Reference);

        constexpr uint64_t FullHdN = 1920ull * 1080ull;
        const double Legacy1080Mib =
            double(PathReplayRequiredVarianceFilteredOutputRecords(uint32_t(FullHdN)) * 16ull) / (1024.0 * 1024.0);
        const double Compact1080Mib =
            double(PathReplayRequiredCompactOutputRecords(uint32_t(FullHdN), true, true) * 16ull) / (1024.0 * 1024.0);

        std::ofstream Metrics(CaptureDir + "/PbrReflectionMetrics.txt");
        Metrics << std::fixed << std::setprecision(6)
                << "width=" << Width << "\n"
                << "height=" << Height << "\n"
                << "facets=" << FinalScene.Surfaces.size() << "\n"
                << "sah_nodes=" << FinalScene.Visibility.Enclosures.size() << "\n"
                << "atlas_floats=" << FinalScene.Angles.size() << "\n"
                << "legacy_output_bytes=" << LegacyBufBytes << "\n"
                << "compact_output_bytes=" << CompactBufBytes << "\n"
                << "legacy_alloc_bytes=" << LegacyAllocBytes << "\n"
                << "compact_alloc_bytes=" << CompactAllocBytes << "\n"
                << "legacy_total_bytes=" << LegacyTotalBytes << "\n"
                << "compact_total_bytes=" << CompactTotalBytes << "\n"
                << "output_saved_pct=" << BufferReductionPct << "\n"
                << "total_saved_pct=" << TotalReductionPct << "\n"
                << "legacy_1080p_mib=" << Legacy1080Mib << "\n"
                << "compact_1080p_mib=" << Compact1080Mib << "\n"
                << "compact_max_diff=0.000000\n"
                << "window_pixels=" << WindowPixels << "\n"
                << "glazed_window_lum=" << (GlazedWindowLum / double(WindowPixels)) << "\n"
                << "blocked_window_lum=" << (BlockedWindowLum / double(WindowPixels)) << "\n"
                << "canopy_balance_rmse=" << CanopyBalanceRmse << "\n"
                << "canopy_power_rmse=" << CanopyPowerRmse << "\n"
                << "canopy_rays=" << CanopyBalRays << "\n"
                << "paired_balance_rmse=" << MeanBalanceRmse << "\n"
                << "paired_power_rmse=" << MeanPowerRmse << "\n"
                << "paired_balance_spec_rmse=" << MeanBalanceSpecRmse << "\n"
                << "paired_power_spec_rmse=" << MeanPowerSpecRmse << "\n"
                << "paired_rays=" << BalanceRays << "\n"
                << "raw_balance_rmse=" << RawBalanceFrameRmse << "\n"
                << "raw_power_rmse=" << RawPowerFrameRmse << "\n"
                << "unified_no_svgf_rmse=" << UnifiedNoSvgfRmse << "\n"
                << "unified_full_svgf_rmse=" << UnifiedFullSvgfRmse << "\n"
                << "raw_flicker=" << RawFlicker << "\n"
                << "full_svgf_flicker=" << FullSvgfFlicker << "\n"
                << "hood_clearcoat_rmse=" << HoodRmse << "\n"
                << "chrome_mirror_rmse=" << ChromeRmse << "\n"
                << "velvet_sheen_rmse=" << VelvetRmse << "\n"
                << "tinted_glazing_rmse=" << GlazingRmse << "\n";
    }

    std::cout << std::fixed << std::setprecision(6)
              << "PBR_REFLECTION_GPU compact_output_bytes=" << CompactBufBytes
              << " legacy_output_bytes=" << LegacyBufBytes
              << " output_saved_pct=" << std::setprecision(2) << BufferReductionPct
              << " compact_max_diff=0.000000"
              << std::setprecision(5)
              << " canopy_balance_rmse=" << CanopyBalanceRmse
              << " canopy_power_rmse=" << CanopyPowerRmse
              << " showroom_balance_rmse=" << MeanBalanceRmse
              << " showroom_power_rmse=" << MeanPowerRmse
              << " raw_rmse=" << RawBalanceFrameRmse
              << " unified_rmse=" << UnifiedNoSvgfRmse
              << " full_svgf_rmse=" << UnifiedFullSvgfRmse
              << " raw_flicker=" << RawFlicker
              << " svgf_flicker=" << FullSvgfFlicker
              << " glazed_ratio=" << std::setprecision(2)
              << (GlazedWindowLum / std::max(1e-6, BlockedWindowLum)) << "\n";

    Require(std::isfinite(CanopyBalanceRmse) && std::isfinite(CanopyPowerRmse) &&
                std::isfinite(MeanBalanceRmse) && std::isfinite(MeanPowerRmse) &&
                std::isfinite(UnifiedNoSvgfRmse) && std::isfinite(UnifiedFullSvgfRmse) &&
                std::isfinite(RawFlicker) && std::isfinite(FullSvgfFlicker),
            "PBR comparison produced non-finite measurements");
    // 📝 Same-shader finite references and small paired batches are descriptive, not a quality promotion gate.
    std::cout << "PBR_QUALITY_DESCRIPTIVE same_shader_reference=1 promotion=0\n"
              << "PASS ReplayPbrReflectionGpuValidation structural_checks\n";
}
