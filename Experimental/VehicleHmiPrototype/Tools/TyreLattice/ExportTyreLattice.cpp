//============================================================================================================================================
//                                                        EXPORTTYRELATTICE.CPP
//============================================================================================================================================
// 🧩 Writes one constructed, prestressed tyre lattice as the record stream the WebGPU preview tiles into a fleet.

#include "SlateCompute/Compute/TyreSolver/Api/TyreSolver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// 📝 The preview does not reconstruct the lattice — the construction is the host's, with its colouring and its
//    linear prestress, and a second spelling in JavaScript would be the duplicated source `00` §4 forbids. One
//    tyre crosses as bytes; the page offsets its particle indices and places each copy at a hub.
//
//    Stream: a four-byte little-endian JSON length, the JSON header, then the fourteen record arrays in the
//    descriptor set's binding order, each exactly as the host holds it.

namespace
{

template <typename Record>
void Append(std::vector<unsigned char>& Stream, const std::vector<Record>& Held)
{
    const unsigned char* Bytes = reinterpret_cast<const unsigned char*>(Held.data());
    Stream.insert(Stream.end(), Bytes, Bytes + Held.size() * sizeof(Record));
}

}   // namespace

int main(int ArgumentCount, char** Arguments)
{
    // 📝 Frontier's change to the engine's exporter: the tyre is declared on the command line, so the lattice matches the vehicle's own tyre
    //    (VehicleSpecification.js: 0.50855 m outer radius, 0.3446 m rim radius, 0.3958 m wide) instead of the preview's 225/45R17.
    //    export_tyre_lattice OUT.bin [outer rim section bead carcassMass pressurePa segments]
    const char* Target = ArgumentCount > 1 ? Arguments[1] : "TyreLattice.bin";

    Slate::TyreSpecification Declared;

    if (ArgumentCount > 2) Declared.OuterRadius       = std::atof(Arguments[2]);
    if (ArgumentCount > 3) Declared.RimRadius         = std::atof(Arguments[3]);
    if (ArgumentCount > 4) Declared.SectionWidth      = std::atof(Arguments[4]);
    if (ArgumentCount > 5) Declared.BeadWidth         = std::atof(Arguments[5]);
    if (ArgumentCount > 6) Declared.CarcassMass       = std::atof(Arguments[6]);
    if (ArgumentCount > 7) Declared.InflationPressure = std::atof(Arguments[7]);
    if (ArgumentCount > 8) Declared.Segments          = static_cast<std::uint32_t>(std::atoi(Arguments[8]));

    const Slate::Deliver<Slate::TyreLattice> Constructed = Slate::ConstructTyreLattice(Declared, 1u);

    if (!Constructed.Resolved)
    {
        std::fprintf(stderr, "refused: %s\n", Constructed.Error.Detail);
        return 1;
    }

    Slate::TyreLattice Lattice = Constructed.Resolve();

    Slate::HubPose Origin;
    Slate::Discard(Slate::PlaceTyre(Lattice, 0u, Origin));

    std::string Header = "{";
    Header += "\"Rings\":" + std::to_string(Lattice.Rings);
    Header += ",\"Segments\":" + std::to_string(Lattice.Segments);
    Header += ",\"ParticleCount\":" + std::to_string(Lattice.ParticleCount());
    Header += ",\"CordCount\":" + std::to_string(Lattice.Cords.size());
    Header += ",\"ShapeRowLimit\":" + std::to_string(Slate::ShapeRowLimit);
    Header += ",\"ShapeSumStride\":" + std::to_string(Slate::ShapeSumStride);
    Header += ",\"ShapeMatrixStride\":" + std::to_string(Slate::ShapeMatrixStride);
    Header += ",\"AccumulatorStride\":" + std::to_string(Slate::TyreAccumulatorStride);
    Header += ",\"GroupExtent\":" + std::to_string(Slate::TyreGroupExtent);
    Header += ",\"RestVolume\":" + std::to_string(Lattice.RestVolume);
    Header += ",\"Spacing\":" + std::to_string(Lattice.Spacing);
    Header += ",\"OuterRadius\":" + std::to_string(Declared.OuterRadius);
    Header += ",\"RimRadius\":" + std::to_string(Declared.RimRadius);
    Header += ",\"BeadWidth\":" + std::to_string(Declared.BeadWidth);
    Header += ",\"LoadFixedScale\":" + std::to_string(Slate::LoadFixedScale);
    Header += ",\"LengthFixedScale\":" + std::to_string(Slate::LengthFixedScale);
    Header += ",\"Colours\":[";

    for (std::size_t Index = 0u; Index < Lattice.Colours.size(); ++Index)
    {
        Header += Index == 0u ? "" : ",";
        Header += "[" + std::to_string(Lattice.Colours[Index].Offset) + "," + std::to_string(Lattice.Colours[Index].Count) + "]";
    }

    Header += "]}";

    std::vector<unsigned char> Stream;
    const std::uint32_t        HeaderBytes = static_cast<std::uint32_t>(Header.size());

    Stream.insert(Stream.end(), reinterpret_cast<const unsigned char*>(&HeaderBytes),
                  reinterpret_cast<const unsigned char*>(&HeaderBytes) + sizeof(HeaderBytes));
    Stream.insert(Stream.end(), Header.begin(), Header.end());

    Append(Stream, Lattice.Particles);
    Append(Stream, Lattice.Motions);
    Append(Stream, Lattice.Velocities);
    Append(Stream, Lattice.Rests);
    Append(Stream, Lattice.Gradients);
    Append(Stream, Lattice.Cords);
    Append(Stream, Lattice.CordMultipliers);
    Append(Stream, Lattice.Hubs);
    Append(Stream, Lattice.Accumulators);
    Append(Stream, Lattice.ShapeRows);
    Append(Stream, Lattice.ShapeSums);
    Append(Stream, Lattice.ShapeMultipliers);
    Append(Stream, Lattice.ShapeAdvances);
    Append(Stream, Lattice.ShapeMatrix);

    std::FILE* Written = std::fopen(Target, "wb");

    if (Written == nullptr)
    {
        std::fprintf(stderr, "could not open %s\n", Target);
        return 1;
    }

    std::fwrite(Stream.data(), 1u, Stream.size(), Written);
    std::fclose(Written);

    std::printf("%s: %zu bytes, %u particles, %zu cords, %zu colours\n",
                Target, Stream.size(), Lattice.ParticleCount(), Lattice.Cords.size(), Lattice.Colours.size());
    return 0;
}
