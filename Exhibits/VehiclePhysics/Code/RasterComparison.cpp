/*==========================================================================================================================================
                                                        RASTERCOMPARISON.CPP
==========================================================================================================================================*/
// 🧩 Identical-camera CPU raster timings and behavioural checks; includes the actual exhibit implementation.

#include <chrono>
#include <cstdlib>

#ifndef RASTER_SOURCE
#define RASTER_SOURCE "SceneRender.cpp"
#endif
#define main VehicleSceneEntry
#include RASTER_SOURCE
#undef main

//------------------------------------------------------------------------------------------------------------------------
//                                                        VALIDATION
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount != 3) return 2;
    VehicleMeshes Geometry;
    if (!LoadVehicle(Arguments[1], Geometry)) return 2;
    Renderer Raster;
    Raster.Init(1280, 720);
    VehicleSolver Vehicle;
    const double Spawn[3] = {0.0, 0.0, 0.55};
    Vehicle.Respawn(Spawn, -0.55);
    VehicleCommand Idle;
    for (int Step = 0; Step < 300; ++Step) Vehicle.Advance(Idle, 1.0 / 120.0);
    std::vector<F3> BodyPositions;
    WheelRenderState Wheels[4];
    PoseVehicle(Vehicle, Geometry, BodyPositions, Wheels);
    const float Heat[4] = {0, 0, 0, 0};
    const F3 Views[3] = {{5.6f, -5.4f, 1.55f}, {-5.9f, -4.8f, 1.45f}, {1.8f, -2.5f, 0.6f}};
    std::vector<double> Durations;
    for (int Repeat = 0; Repeat < 10; ++Repeat)
    {
        for (int Pose = 0; Pose < 3; ++Pose)
        {
            Raster.Cam = LookAt(Views[Pose], {0.1f, 0.0f, 0.42f}, 42.0f, 1280.0f / 720.0f);
            const auto Start = std::chrono::steady_clock::now();
            Raster.Background();
            Raster.GroundPlane();
            RenderVehicle(Raster, Geometry, BodyPositions, Wheels, false, Heat);
            const double Milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - Start).count();
            if (Repeat > 0) Durations.push_back(Milliseconds);
            if (Repeat == 0)
            {
                const std::string Path = std::string(Arguments[2]) + "/view-" + std::to_string(Pose) + ".ppm";
                if (!Raster.WritePpm(Path)) return 3;
            }
        }
    }
    std::sort(Durations.begin(), Durations.end());
    std::printf("CPU raster 1280x720: 3 fixed poses, 3 warmups, 27 measured, no file IO or simulation in timing\n");
    std::printf("median_ms=%.3f min_ms=%.3f max_ms=%.3f retained_buffer_bytes=%zu\n",
                Durations[Durations.size() / 2], Durations.front(), Durations.back(),
                Raster.Colour.size() * sizeof(float) + Raster.Depth.size() * sizeof(float) +
                Raster.WorldPos.size() * sizeof(F3));
    for (float Radiance : Raster.Colour)
        if (!std::isfinite(Radiance) || Radiance < 0.0f) return 4;
#ifndef RASTER_BASELINE
    // Every downward ray should reach the ground, including rays whose old tiles crossed the near plane.
    Raster.Cam = LookAt({0, -3, 1}, {0, 0, 0.4f}, 60.0f, 1280.0f / 720.0f);
    Raster.Background(); Raster.GroundPlane();
    size_t GroundSamples = 0; float WorstProjection = 0.0f;
    for (int Y = 0; Y < Raster.H; Y += 13)
        for (int X = 0; X < Raster.W; X += 17)
        {
            const float Vertical = (1.0f - 2.0f * (Y + 0.5f) / Raster.H) * Raster.Cam.TanHalfFovY;
            const float RayZ = Raster.Cam.Forward.Z + Raster.Cam.Up.Z * Vertical;
            if (RayZ >= -0.01f) continue;
            const size_t I = static_cast<size_t>(Y) * Raster.W + X;
            if (Raster.WorldPos[I].Z != 0.0f || !std::isfinite(Raster.Depth[I])) return 13;
            float SX, SY; const float Z = Raster.Project(Raster.WorldPos[I], SX, SY);
            if (!std::isfinite(Z)) return 14;
            WorstProjection = std::max({WorstProjection, std::abs(SX-(X+0.5f)),
                                       std::abs(SY-(Raster.H-Y-0.5f))});
            ++GroundSamples;
        }
    if (GroundSamples < 100 || WorstProjection > 0.1f) return 15;
    std::printf("PASS ground ray coverage %zu samples; reprojection error %.5g pixels\n", GroundSamples, WorstProjection);
    // Shadow is applied once, affects only ground and preserves more blue skylight than red sunlight.
    Raster.Cam = LookAt({0, -3, 3}, {0, 0, 0}, 60.0f, 1280.0f / 720.0f);
    Raster.Background();
    std::fill(Raster.Colour.begin(), Raster.Colour.end(), 1.0f);
    std::fill(Raster.WorldPos.begin(), Raster.WorldPos.end(), F3{0, 0, 0});
    Raster.ShadowTriangle({-1, -1, 1}, {1, -1, 1}, {0, 1, 1});
    const auto Once = Raster.Colour;
    Raster.ShadowTriangle({-1, -1, 1}, {1, -1, 1}, {0, 1, 1});
    if (Once != Raster.Colour) return 5;
    size_t ShadowPixels = 0;
    for (size_t Index = 0; Index < Once.size(); Index += 3)
    {
        if (Once[Index] < 1.0f)
        {
            ++ShadowPixels;
            if (!(Once[Index] < Once[Index + 2])) return 6;
        }
    }
    if (ShadowPixels == 0) return 7;
    std::fill(Raster.Colour.begin(), Raster.Colour.end(), 1.0f);
    std::fill(Raster.WorldPos.begin(), Raster.WorldPos.end(), F3{0, 0, 1});
    Raster.ShadowTriangle({-1, -1, 1}, {1, -1, 1}, {0, 1, 1});
    for (float Radiance : Raster.Colour) if (Radiance != 1.0f) return 8;
    Raster.SunDir.Z = 0.0f;
    std::fill(Raster.WorldPos.begin(), Raster.WorldPos.end(), F3{0, 0, 0});
    Raster.ShadowTriangle({-1, -1, 1}, {1, -1, 1}, {0, 1, 1});
    for (float Radiance : Raster.Colour) if (Radiance != 1.0f) return 9;
    // The former post-tonemap exposure clipped every input >= 2 to display white.
    Raster.Init(4, 1);
    for (int Pixel = 0; Pixel < 4; ++Pixel)
        for (int Channel = 0; Channel < 3; ++Channel)
            Raster.Colour[Pixel * 3 + Channel] = static_cast<float>(1 << Pixel);
    const std::string RampPath = std::string(Arguments[2]) + "/highlight-ramp.ppm";
    if (!Raster.WritePpm(RampPath)) return 10;
    FILE* RampFile = std::fopen(RampPath.c_str(), "rb");
    if (RampFile == nullptr) return 11;
    std::fseek(RampFile, -12, SEEK_END);
    unsigned char Ramp[12];
    const size_t ByteCount = std::fread(Ramp, 1, 12, RampFile);
    std::fclose(RampFile);
    if (ByteCount != 12 || !(Ramp[0] < Ramp[3] && Ramp[3] < Ramp[6] && Ramp[6] < Ramp[9] && Ramp[9] < 255))
        return 12;
    std::printf("PASS highlight ramp 1/2/4/8 -> %u/%u/%u/%u (no premature white clipping)\n",
                Ramp[0], Ramp[3], Ramp[6], Ramp[9]);
    std::printf("PASS finite radiance; once-only shadow; blue fill retained (%zu pixels); non-ground unchanged; horizon safe\n",
                ShadowPixels);
#endif
    return 0;
}
