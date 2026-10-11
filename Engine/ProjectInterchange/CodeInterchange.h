//============================================================================================================================================
//                                                      CODEINTERCHANGE.H
//============================================================================================================================================
// 📦 Loads one verified project code image and confines its C ABI lifetime to FrontierHost.

#pragma once

#include "ProjectInterchange.h"
#include "GeometryInterchange.h"
#include "ProjectSpecification.h"

#include <string>

namespace Frontier
{

/// 📦 Dynamic project code-image lifetime; the image remains resident until project records are retired.
class CodeInterchange
{
public:
    CodeInterchange() = default;
    ~CodeInterchange();

    CodeInterchange(const CodeInterchange&) = delete;
    CodeInterchange& operator=(const CodeInterchange&) = delete;

    /// 📦 Opens and verifies an optional code image named by the resolved specification.
    [[nodiscard]] bool Construct(
        const ProjectSpecification& ResolvedSpecification,
        std::string& Refusal);

    /// 📦 Delivers project-opening readings to verified project code after the host owns its shared facilities.
    [[nodiscard]] bool ConstructProject(
        const ProjectSpecification& ResolvedSpecification,
        const FrontierProjectHostInterchange& HostInterchange,
        std::string& Refusal);

    /// 📦 Advances project simulation once while the shared renderer retains window and device ownership.
    [[nodiscard]] bool AdvanceProject(
        float ElapsedSeconds,
        float CycleSeconds,
        const FrontierProjectInputReading* InputReading,
        std::string& Refusal) const;

    /// 📦 Retires project-owned records before closing their code image.
    void Retire() noexcept;

    [[nodiscard]] bool HasGeometry() const noexcept { return GeometryEntry != nullptr; }
    uint32_t ProjectGeometry(FrontierProjectGeometryReading& Reading) const
    { return GeometryEntry ? GeometryEntry(ProjectRecord, &Reading) : 0u; }

    [[nodiscard]] bool CodeImageOpen() const noexcept;

private:
    FrontierProjectGeometry   GeometryEntry = nullptr;
    void*                     CodeImage = nullptr;
    FrontierProjectInterchange ActiveInterchange{};
    void*                     ProjectRecord = nullptr;
};

} // namespace Frontier
