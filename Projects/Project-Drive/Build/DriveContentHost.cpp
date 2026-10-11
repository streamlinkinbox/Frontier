//============================================================================================================================================
//                                                          DRIVECONTENTHOST.CPP
//============================================================================================================================================
// 📦 Headless export and import verification of Project-Drive's existing authored opening scene.

#include "../Source/DriveSceneAuthor.h"
#include "../Source/TyreSequence.h"
#include "../../../Engine/PhysicalDynamics/Vehicle/VehicleGeometry.h"
#include "../../../Engine/ContentInterchange/SceneCodec.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <fstream>
#include <sstream>

/// 📦 Exports absent project content and verifies it through the native glTF importer.
/// err   returns nonzero for malformed arguments, export failure or unreadable content
int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount != 3 || (std::string(Arguments[1]) != "--ensure" && std::string(Arguments[1]) != "--verify-only"))
    {
        std::fputs("Usage: DriveContentHost --ensure|--verify-only DriveCourse.gltf\n", stderr);
        return 64;
    }

    try
    {
        const std::filesystem::path SceneLocation = std::filesystem::absolute(Arguments[2]);
        if (!std::filesystem::exists(SceneLocation) && std::string(Arguments[1]) == "--ensure")
        {
            std::filesystem::create_directories(SceneLocation.parent_path());
            Frontier::Drive::DriveSceneAuthor Author;
            // 📝 Export the settled opening presentation; Play later supplies separate live placement deltas.
            Author.Construct(true);
            std::string Refusal;
            if (!Author.Export(SceneLocation.string(), &Refusal))
            {
                std::fprintf(stderr, "Drive scene export failed: %s\n", Refusal.c_str());
                return 1;
            }
        }

        Frontier::SceneStructure Scene;
        Frontier::TextureIndex Textures;
        std::string Refusal;
        if (!Frontier::SceneCodec::Decode(SceneLocation.string(), Scene, &Textures, {}, &Refusal))
        {
            std::fprintf(stderr, "Drive scene import failed: %s\n", Refusal.c_str());
            return 1;
        }
        if (Scene.QueryTriangleCount() == 0 || Scene.QueryPlacements().size() < 6)
        {
            std::fputs("Drive scene is missing its authored vehicle, wheels or course\n", stderr);
            return 1;
        }
        // A project-owned rest bake, not a replacement renderer. Live host geometry reconstructs the production
        // hybrid world field; this local signed envelope is retained for field-only queries and diagnostics.
        const Frontier::Vehicle::VehicleGeometry Geometry;
        Frontier::Vehicle::SoftTyreParameters Parameters;
        Parameters.Radius = Geometry.TyreRadius;
        Parameters.RimRadius = Geometry.TyreRimRadius;
        Parameters.Width = Geometry.TyreWidth;
        Frontier::Vehicle::XPBDSoftTyre Tyre;
        Tyre.Build(Parameters, {}, {});
        Frontier::Drive::TyreSequence Surface;
        Surface.Capture(Tyre, {}, {});
        std::vector<Frontier::Vector3> Vertices;
        std::vector<Frontier::DeformationSpace::Face> Faces;
        Surface.Envelope(Vertices, Faces, true);
        Frontier::DeformationSpace Bake;
        if (!Bake.Update(Vertices, Faces, 64u, 0.06f, Refusal))
        {
            std::fprintf(stderr, "Drive rest SDF refused: %s\n", Refusal.c_str());
            return 1;
        }
        const auto BakeFolder = SceneLocation.parent_path().parent_path() / "DistanceFields";
        const auto BakeLocation = BakeFolder / "DriveTyre.sdf";
        const auto KeyLocation = BakeFolder / "DriveTyre.signature";
        uint64_t PreviousSignature = 0u;
        std::ifstream(KeyLocation) >> PreviousSignature;
        std::ostringstream ExpectedStream(std::ios::out | std::ios::binary);
        if (!Bake.QueryField().WriteToStream(ExpectedStream)) return 1;
        const std::string Expected = ExpectedStream.str();
        bool Matches = false;
        if (PreviousSignature == Bake.Signature() && std::filesystem::exists(BakeLocation) &&
            std::filesystem::file_size(BakeLocation) == Expected.size())
        {
            std::string Existing(Expected.size(), '\0');
            std::ifstream Previous(BakeLocation, std::ios::binary);
            Previous.read(Existing.data(), static_cast<std::streamsize>(Existing.size()));
            Matches = Previous.good() && Existing == Expected;
        }
        if (!Matches)
        {
            if (std::string(Arguments[1]) == "--verify-only")
            {
                std::fputs("Drive rest SDF is absent, stale or corrupt\n", stderr);
                return 1;
            }
            std::filesystem::create_directories(BakeFolder);
            if (!Bake.QueryField().SaveToFile(BakeLocation.string())) return 1;
            std::ofstream Key(KeyLocation);
            Key << Bake.Signature() << "\n";
            if (!Key) return 1;
        }
        std::printf("PASS DriveTyre SDF1 rest bake: %zu samples; signature %llu; %s\n",
                    Bake.QueryField().GetSampleCount(), static_cast<unsigned long long>(Bake.Signature()),
                    Matches ? "verified existing" : "rebuilt");
        std::printf("PASS DriveCourse import: %u triangles, %zu placements, %zu instances\n",
                    Scene.QueryTriangleCount(), Scene.QueryPlacements().size(), Scene.QueryInstances().size());
        return 0;
    }
    catch (const std::exception& Refusal)
    {
        std::fprintf(stderr, "Drive content refused: %s\n", Refusal.what());
        return 1;
    }
}
