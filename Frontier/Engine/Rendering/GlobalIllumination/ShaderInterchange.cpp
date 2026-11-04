/*==========================================================================================================================================
                                                         SHADERINTERCHANGE.CPP
==========================================================================================================================================*/
// 🧩 SPIR-V program construction for the ported Slang units, with the host scene constants as specialization values.

#include "ShaderInterchange.h"

#include <stdexcept>

namespace Frontier::GlobalIllumination
{

ShaderInterchange::ShaderInterchange(DeviceExchange& Exchange, std::filesystem::path SpirvFolder)
    : Exchange_(Exchange), SpirvFolder_(std::move(SpirvFolder))
{
}

std::string ShaderInterchange::QuerySpirvPath(const std::string& Unit, const std::string& Entry) const
{
    return (SpirvFolder_ / (Unit + "." + Entry + ".spv")).string();
}

std::shared_ptr<DeviceProgram> ShaderInterchange::ConstructComputeProgram(const std::string& Unit, const std::string& Entry,
                                                                          std::vector<std::shared_ptr<DescriptorLayout>> Layouts)
{
    auto Program = Exchange_.ConstructComputeProgram(QuerySpirvPath(Unit, Entry), Entry, std::move(Layouts), Constants_, &Diagnostic_);
    if (!Program) throw std::runtime_error("compute program " + Unit + ":" + Entry + " — " + Diagnostic_);
    return Program;
}

std::shared_ptr<DeviceProgram> ShaderInterchange::ConstructRasterProgram(const std::string& Unit, RasterProgramSpecification Specification)
{
    Specification.VertexSpirvPath = QuerySpirvPath(Unit, Specification.VertexEntry);
    if (!Specification.FragmentEntry.empty())
    {
        Specification.FragmentSpirvPath = QuerySpirvPath(Unit, Specification.FragmentEntry);
    }
    Specification.Specialization = Constants_;
    if (Specification.Label.empty()) Specification.Label = Unit + ":" + Specification.VertexEntry;
    auto Program = Exchange_.ConstructRasterProgram(Specification, &Diagnostic_);
    if (!Program) throw std::runtime_error("raster program " + Unit + ":" + Specification.VertexEntry + " — " + Diagnostic_);
    return Program;
}

} // namespace Frontier::GlobalIllumination
