#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "Engine/Render/VectorSurfaceCompilation.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
}

int main(int ArgumentCount, char **Arguments)
{
    try
    {
        if (ArgumentCount != 2) throw std::runtime_error("Usage: VectorAuthoringImportValidation <native.vector.toml>");
        std::ifstream Input(Arguments[1], std::ios::binary);
        if (!Input) throw std::runtime_error("Cannot open generated native Pattern TOML.");
        const std::string Source(std::istreambuf_iterator<char>(Input), {});
        Require(!Source.empty() && Source.size() <= 4000000, "Generated native Pattern TOML is empty or exceeds its source bound.");
        const auto Document = Frontier::DecodeVectorAuthoring(Source);
        Require(Document.Name == "Native importer acceptance fixture", "Pattern name did not round-trip.");
        Require(Document.Layers.size() == 5, "Pattern layer count did not round-trip.");
        Require(Document.Layers[0].Name == "Pattern background" &&
            Document.Layers[0].SurfaceChannels[size_t(Frontier::VectorAuthoringSurfaceChannel::Colour)].Enabled,
            "Solid Pattern background was not imported as an independent colour-only layer.");
        Require(Document.Layers[1].Mask.has_value() && Document.Layers[1].MaskEnabled,
            "Supported native shape mask was not imported.");
        Require(std::abs(Document.Instructions[1].Colour[0] - 85.f / 255.f) < .00001f,
            "Pattern sRGB paint was not retained for the Float32 channel conversion.");
        Require(Document.Layers[2].SurfaceStructure == Frontier::VectorAuthoringSurfaceStructure::Stitch &&
            Document.Layers[2].TextileGeometry == Frontier::VectorAuthoringTextileGeometry::StitchThread,
            "Golden thread identity was not retained independently of colour.");
        const auto HeightIndex = size_t(Frontier::VectorAuthoringSurfaceChannel::Height);
        const auto NormalIndex = size_t(Frontier::VectorAuthoringSurfaceChannel::Normal);
        const auto RoughnessIndex = size_t(Frontier::VectorAuthoringSurfaceChannel::Roughness);
        const auto MetalnessIndex = size_t(Frontier::VectorAuthoringSurfaceChannel::Metalness);
        Require(Document.Layers[2].SurfaceChannels[HeightIndex].Enabled &&
            Document.Layers[2].SurfaceChannels[HeightIndex].Strength == .75f &&
            Document.Layers[2].SurfaceChannels[NormalIndex].Enabled &&
            Document.Layers[2].SurfaceChannels[NormalIndex].Strength == 1.25f,
            "Textile Height/Normal contribution metadata did not round-trip.");
        Require(Document.Layers[2].SurfaceChannels[RoughnessIndex].Strength == .24f &&
            Document.Layers[2].SurfaceChannels[MetalnessIndex].Strength == .92f,
            "Per-shape dielectric/golden-thread scalar material values did not round-trip.");
        const auto SeamRoot = Document.Layers[4].Root;
        Require(Document.Instructions[SeamRoot].Topology[0] == 28,
            "Wrapped geometry copies were not composed into one retained layer root.");
        const auto FirstCopy = Document.Instructions[Document.Instructions[SeamRoot].Topology[1]];
        const auto SecondCopy = Document.Instructions[Document.Instructions[SeamRoot].Topology[2]];
        Require(FirstCopy.Topology[0] == 6 && SecondCopy.Topology[0] == 6 &&
            FirstCopy.Bounds[0] == 4.f && SecondCopy.Bounds[0] == 516.f,
            "Straight XY seam copies did not land at their exact tile offsets.");
        (void)Frontier::ConstructVectorAuthoringCoverage(Document);
        constexpr std::array Channels{
            Frontier::VectorAuthoringSurfaceChannel::Colour,
            Frontier::VectorAuthoringSurfaceChannel::Height,
            Frontier::VectorAuthoringSurfaceChannel::Normal,
            Frontier::VectorAuthoringSurfaceChannel::Roughness,
            Frontier::VectorAuthoringSurfaceChannel::Metalness,
            Frontier::VectorAuthoringSurfaceChannel::Opacity,
            Frontier::VectorAuthoringSurfaceChannel::Occlusion};
        for (const auto Channel : Channels) (void)Frontier::CompileVectorSurface(Document, Channel);
        std::cout << "Native Pattern import passed: " << Document.Layers.size() << " layers, "
            << Document.Instructions.size() << " instructions, " << Document.Segments.size()
            << " cubic segments, seven compiled Float32 surface channels.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "Native Pattern import validation failed: " << Failure.what() << '\n';
        return 1;
    }
}
