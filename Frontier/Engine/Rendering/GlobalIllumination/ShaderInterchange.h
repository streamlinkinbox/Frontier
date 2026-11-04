/*==========================================================================================================================================
                                                          SHADERINTERCHANGE.H
==========================================================================================================================================*/
// 🧩 SPIR-V program construction for the ported Slang units, with the host scene constants as specialization values.

#pragma once

#include "DeviceExchange/DeviceExchange.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::GlobalIllumination
{

// 📝 The demo assembles WGSL at run time and prepends the host constants (IlluminationHost.js "Constants") to every
//    module built after the card depot exists. The port compiles each Slang entry offline to
//    <SpirvFolder>/<Unit>.<Entry>.spv and hands the same constants over as specialization values, in the order of
//    HostConstants.slang (constant_id = position).
class ShaderInterchange
{
  public:
    ShaderInterchange(DeviceExchange& Exchange, std::filesystem::path SpirvFolder);

    void ConfigureConstants(std::vector<SpecializationConstant> Constants) { Constants_ = std::move(Constants); }
    [[nodiscard]] const std::vector<SpecializationConstant>& Constants() const noexcept { return Constants_; }
    [[nodiscard]] std::string QuerySpirvPath(const std::string& Unit, const std::string& Entry) const;
    [[nodiscard]] const std::filesystem::path& SpirvFolder() const noexcept { return SpirvFolder_; }

    std::shared_ptr<DeviceProgram> ConstructComputeProgram(const std::string& Unit, const std::string& Entry,
                                                           std::vector<std::shared_ptr<DescriptorLayout>> Layouts);
    std::shared_ptr<DeviceProgram> ConstructRasterProgram(const std::string& Unit, RasterProgramSpecification Specification);

    [[nodiscard]] const std::string& LastDiagnostic() const noexcept { return Diagnostic_; }

  private:
    DeviceExchange&                  Exchange_;
    std::filesystem::path            SpirvFolder_;
    std::vector<SpecializationConstant> Constants_{};
    std::string                      Diagnostic_{};
};

} // namespace Frontier::GlobalIllumination
