//============================================================================================================================================
//                                                     CODEINTERCHANGE.CPP
//============================================================================================================================================
// 📦 Platform code-image acquisition and C-layout verification for project-specific behaviour.

#include "CodeInterchange.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{

size_t BoundedTextLength(const char* Text, size_t Capacity)
{
    size_t CharacterCount = 0u;
    while (CharacterCount < Capacity && Text[CharacterCount] != '\0')
        ++CharacterCount;

    return CharacterCount;
}

void CopyRefusal(const FrontierProjectRefusal& Source, std::string& Destination)
{
    Destination.assign(Source.Explanation, BoundedTextLength(Source.Explanation, sizeof(Source.Explanation)));
    if (Destination.empty())
        Destination = "project code image refused the requested interchange without an explanation";
}

void* AcquireCodeImage(const std::filesystem::path& CodeImageLocation, std::string& Refusal)
{
#if defined(_WIN32)
    HMODULE ActiveCodeImage = LoadLibraryW(CodeImageLocation.wstring().c_str());
    if (ActiveCodeImage == nullptr)
    {
        Refusal = "LoadLibraryW could not open project code image: " + CodeImageLocation.string();
        return nullptr;
    }

    return ActiveCodeImage;
#else
    void* ActiveCodeImage = dlopen(CodeImageLocation.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (ActiveCodeImage == nullptr)
    {
        const char* DiagnosticText = dlerror();
        Refusal = "dlopen could not open project code image: " + CodeImageLocation.string() +
                  (DiagnosticText == nullptr ? std::string{} : "; " + std::string(DiagnosticText));
        return nullptr;
    }

    return ActiveCodeImage;
#endif
}

void ReleaseCodeImage(void* CodeImage) noexcept
{
    if (CodeImage == nullptr)
        return;

#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(CodeImage));
#else
    dlclose(CodeImage);
#endif
}

FrontierConstructProjectInterchange QueryConstructionEntry(void* CodeImage)
{
#if defined(_WIN32)
    return reinterpret_cast<FrontierConstructProjectInterchange>(
        GetProcAddress(static_cast<HMODULE>(CodeImage), "ConstructProjectInterchange"));
#else
    return reinterpret_cast<FrontierConstructProjectInterchange>(dlsym(CodeImage, "ConstructProjectInterchange"));
#endif
}

bool InterchangeAccepted(const FrontierProjectInterchange& ActiveInterchange, std::string& Refusal)
{
    if (ActiveInterchange.StructureSize < sizeof(FrontierProjectInterchange))
    {
        Refusal = "project code image returned a truncated FrontierProjectInterchange record";
        return false;
    }

    if (ActiveInterchange.CodeInterchangeNumber != FrontierCodeInterchangeNumber)
    {
        Refusal = "project code image returned an incompatible code interchange number";
        return false;
    }

    if (ActiveInterchange.InterfaceFingerprint != FrontierCodeInterchangeFingerprint)
    {
        Refusal = "project code image returned an incompatible interface fingerprint";
        return false;
    }

    if (ActiveInterchange.ConstructProject == nullptr || ActiveInterchange.AdvanceProject == nullptr ||
        ActiveInterchange.RetireProject == nullptr)
    {
        Refusal = "project code image omitted one required project lifecycle callback";
        return false;
    }

    return true;
}

} // namespace

namespace Frontier
{

CodeInterchange::~CodeInterchange()
{
    Retire();
}

bool CodeInterchange::Construct(const ProjectSpecification& ResolvedSpecification, std::string& Refusal)
{
    Retire();
    Refusal.clear();
    if (!ResolvedSpecification.CodeImageDeclared())
        return true;

    CodeImage = AcquireCodeImage(ResolvedSpecification.CodeImageLocation, Refusal);
    if (CodeImage == nullptr)
        return false;

#if defined(_WIN32)
    GeometryEntry = reinterpret_cast<FrontierProjectGeometry>(
        GetProcAddress(static_cast<HMODULE>(CodeImage), "ProjectGeometryInterchange"));
#else
    GeometryEntry = reinterpret_cast<FrontierProjectGeometry>(dlsym(CodeImage, "ProjectGeometryInterchange"));
#endif
    const FrontierConstructProjectInterchange ConstructInterchange = QueryConstructionEntry(CodeImage);
    if (ConstructInterchange == nullptr)
    {
        Refusal = "project code image does not export ConstructProjectInterchange";
        Retire();
        return false;
    }

    FrontierProjectRefusal ProjectRefusal{};
    ActiveInterchange.StructureSize = sizeof(FrontierProjectInterchange);
    const uint32_t ConstructionAccepted = ConstructInterchange(
        FrontierCodeInterchangeNumber,
        FrontierCodeInterchangeFingerprint,
        &ActiveInterchange,
        &ProjectRefusal);
    if (ConstructionAccepted == 0u)
    {
        CopyRefusal(ProjectRefusal, Refusal);
        Retire();
        return false;
    }

    if (!InterchangeAccepted(ActiveInterchange, Refusal))
    {
        Retire();
        return false;
    }

    return true;
}

bool CodeInterchange::ConstructProject(
    const ProjectSpecification& ResolvedSpecification,
    const FrontierProjectHostInterchange& HostInterchange,
    std::string& Refusal)
{
    Refusal.clear();
    if (!CodeImageOpen())
        return true;

    const std::string SpecificationLocationText = ResolvedSpecification.SpecificationLocation.string();
    const std::string ContentLocationText = ResolvedSpecification.ContentLocation.string();
    const std::string OpeningSceneLocationText = ResolvedSpecification.OpeningSceneLocation.string();

    FrontierProjectLaunch ActiveLaunch{};
    ActiveLaunch.StructureSize = sizeof(FrontierProjectLaunch);
    ActiveLaunch.ProjectName = ResolvedSpecification.ProjectName.c_str();
    ActiveLaunch.SpecificationLocation = SpecificationLocationText.c_str();
    ActiveLaunch.ContentLocation = ContentLocationText.c_str();
    ActiveLaunch.OpeningSceneLocation = OpeningSceneLocationText.c_str();

    FrontierProjectRefusal ProjectRefusal{};
    void* ConstructedProjectRecord = nullptr;
    const uint32_t ConstructionAccepted = ActiveInterchange.ConstructProject(
        &ActiveLaunch,
        &HostInterchange,
        &ConstructedProjectRecord,
        &ProjectRefusal);
    if (ConstructionAccepted == 0u)
    {
        CopyRefusal(ProjectRefusal, Refusal);
        return false;
    }

    ProjectRecord = ConstructedProjectRecord;
    return true;
}

bool CodeInterchange::AdvanceProject(
    float ElapsedSeconds,
    float CycleSeconds,
    const FrontierProjectInputReading* InputReading,
    std::string& Refusal) const
{
    Refusal.clear();
    if (!CodeImageOpen())
        return true;

    FrontierProjectCycle ActiveCycle{};
    ActiveCycle.StructureSize = sizeof(FrontierProjectCycle);
    ActiveCycle.ElapsedSeconds = ElapsedSeconds;
    ActiveCycle.CycleSeconds = CycleSeconds;
    ActiveCycle.InputReading = InputReading;

    FrontierProjectRefusal ProjectRefusal{};
    if (ActiveInterchange.AdvanceProject(ProjectRecord, &ActiveCycle, &ProjectRefusal) == 0u)
    {
        CopyRefusal(ProjectRefusal, Refusal);
        return false;
    }

    return true;
}

void CodeInterchange::Retire() noexcept
{
    if (ProjectRecord != nullptr && ActiveInterchange.RetireProject != nullptr)
        ActiveInterchange.RetireProject(ProjectRecord);

    ProjectRecord = nullptr;
    GeometryEntry = nullptr;
    ActiveInterchange = {};
    ReleaseCodeImage(CodeImage);
    CodeImage = nullptr;
}

bool CodeInterchange::CodeImageOpen() const noexcept
{
    return CodeImage != nullptr;
}

} // namespace Frontier
