#!/usr/bin/env python3
#============================================================================================================================================
#                                           CREATEPROJECTSPECIFICATION.PY
#============================================================================================================================================
# 📦 Creates an independent .frontier specification, content folders, and minimal C ABI project code image.

from __future__ import annotations

import argparse
import re
from pathlib import Path


ProjectNamePattern = re.compile(r"Project-[A-Za-z][A-Za-z0-9-]*$")


InterchangeSource = '''//============================================================================================================================================
//                                             {IMAGE_NAME}INTERCHANGE.CPP
//============================================================================================================================================
// 📦 Standalone project code image; Frontier.exe retains all shared facility ownership.

#include "../../../Engine/ProjectInterchange/ProjectInterchange.h"

#include <cstring>

namespace
{{

uint32_t FRONTIER_CODE_IMAGE_CALL ConstructProject(
    const FrontierProjectLaunch* ActiveLaunch,
    const FrontierProjectHostInterchange* HostInterchange,
    void** ProjectRecord,
    FrontierProjectRefusal* Refusal)
{{
    (void)ActiveLaunch;
    (void)HostInterchange;
    (void)Refusal;
    if (ProjectRecord == nullptr)
        return 0u;

    *ProjectRecord = nullptr;
    return 1u;
}}

uint32_t FRONTIER_CODE_IMAGE_CALL AdvanceProject(
    void* ProjectRecord,
    const FrontierProjectCycle* ActiveCycle,
    FrontierProjectRefusal* Refusal)
{{
    (void)ProjectRecord;
    (void)Refusal;
    return ActiveCycle != nullptr && ActiveCycle->StructureSize >= sizeof(FrontierProjectCycle) ? 1u : 0u;
}}

void FRONTIER_CODE_IMAGE_CALL RetireProject(void* ProjectRecord)
{{
    (void)ProjectRecord;
}}

}} // namespace

extern "C" FRONTIER_CODE_IMAGE_EXPORT uint32_t FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t RequestedInterchangeNumber,
    uint64_t RequestedFingerprint,
    FrontierProjectInterchange* DeliveredInterchange,
    FrontierProjectRefusal* Refusal)
{{
    (void)Refusal;
    if (DeliveredInterchange == nullptr || RequestedInterchangeNumber != FrontierCodeInterchangeNumber ||
        RequestedFingerprint != FrontierCodeInterchangeFingerprint)
        return 0u;

    std::memset(DeliveredInterchange, 0, sizeof(*DeliveredInterchange));
    DeliveredInterchange->StructureSize = sizeof(FrontierProjectInterchange);
    DeliveredInterchange->CodeInterchangeNumber = FrontierCodeInterchangeNumber;
    DeliveredInterchange->InterfaceFingerprint = FrontierCodeInterchangeFingerprint;
    DeliveredInterchange->ConstructProject = &ConstructProject;
    DeliveredInterchange->AdvanceProject = &AdvanceProject;
    DeliveredInterchange->RetireProject = &RetireProject;
    return 1u;
}}
'''


def WriteNewFile(TargetLocation: Path, Text: str) -> None:
    if TargetLocation.exists():
        raise SystemExit(f"refusing to replace existing file: {TargetLocation}")
    TargetLocation.write_text(Text, encoding="utf-8")


def main() -> int:
    ArgumentParser = argparse.ArgumentParser(description="Create an independent Frontier project specification and code image.")
    ArgumentParser.add_argument("name", help="project folder name, for example Project-Tractrix")
    ArgumentParser.add_argument("--root", type=Path, default=Path.cwd())
    Arguments = ArgumentParser.parse_args()

    if ProjectNamePattern.fullmatch(Arguments.name) is None:
        raise SystemExit("project name must follow Project-Subject using letters, numbers, and hyphens")

    RepositoryRoot = Arguments.root.resolve()
    ProjectLocation = RepositoryRoot / "Projects" / Arguments.name
    if ProjectLocation.exists():
        raise SystemExit(f"refusing to replace existing project: {ProjectLocation}")

    ImageName = Arguments.name.replace("-", "")
    ProjectLocation.mkdir(parents=True)
    (ProjectLocation / "Content" / "Scenes").mkdir(parents=True)
    (ProjectLocation / "Source").mkdir(parents=True)
    (ProjectLocation / "Build").mkdir(parents=True)

    SpecificationText = "\n".join(
        (
            "[Project]",
            f'ProjectName             = "{ImageName}"',
            "ProjectFormatNumber     = 1",
            'ContentLocation         = "Content"',
            'OpeningScene            = "Content/Scenes/Opening.gltf"',
            f'CodeImage               = "Build/{ImageName}.dll"',
            "CodeInterchangeNumber   = 1",
            "InterfaceFingerprint    = 0x4f0b3cbd4fd15f29",
            "",
        )
    )
    WriteNewFile(ProjectLocation / f"{ImageName}.frontier", SpecificationText)
    WriteNewFile(ProjectLocation / "Source" / f"{ImageName}Interchange.cpp", InterchangeSource.format(IMAGE_NAME=ImageName.upper()))
    WriteNewFile(ProjectLocation / "Build" / ".gitignore", "*.dll\n*.lib\n*.exp\n*.pdb\n")
    WriteNewFile(ProjectLocation / "Content" / "Scenes" / ".gitkeep", "")

    print(f"created {ProjectLocation.relative_to(RepositoryRoot)} with its own .frontier stream and code image")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
