#!/usr/bin/env python3
#============================================================================================================================================
#                                                PROJECTOWNERSHIPCHECKS.PY
#============================================================================================================================================
# 📦 Static architecture gates preventing project hosts, source copying, and Project-Zero coupling from returning.

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


WindowedEntryPattern = re.compile(r"\bint\s+main\s*\(")
# A project-side CPU evidence harness can legitimately include Vulkan-shaped headers or
# shared engine structures while still being headless. Gate actual window/swapchain
# ownership instead of the broad word "Vulkan", which previously rejected those
# headless references as false project hosts.
WindowedFacilityPattern = re.compile(
    r"SwapchainExchange|glfw(?:Init|CreateWindow|WindowHint|MakeContextCurrent)|"
    r"ImGui_Impl|vkCreate(?:Win32|Xcb|Xlib|Wayland|Metal|Android)?SurfaceKHR|vkCreateSwapchainKHR",
    re.IGNORECASE,
)
ProjectSourcePattern = re.compile(r"(?:Projects[\\/]|\.\.[\\/])+Project-[A-Za-z0-9-]+[\\/]Source")


def ReadText(SourceLocation: Path) -> str:
    return SourceLocation.read_text(encoding="utf-8", errors="replace")


def RecordFailure(Failures: list[str], Explanation: str) -> None:
    Failures.append(Explanation)


def RemoveCppComments(SourceText: str) -> str:
    """Prevent prose describing a swapchain from being classified as a swapchain owner."""
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", SourceText, flags=re.DOTALL)


def CheckWindowedEntries(RepositoryRoot: Path, Failures: list[str]) -> None:
    ExpectedEntry = RepositoryRoot / "Engine" / "Host" / "FrontierExecution.cpp"
    if not ExpectedEntry.is_file() or not WindowedEntryPattern.search(ReadText(ExpectedEntry)):
        RecordFailure(Failures, "Engine/Host/FrontierExecution.cpp must be the sole Frontier.exe entry")

    for SourceLocation in sorted((RepositoryRoot / "Projects").glob("**/*.cpp")):
        SourceText = RemoveCppComments(ReadText(SourceLocation))
        if WindowedEntryPattern.search(SourceText) and WindowedFacilityPattern.search(SourceText):
            RecordFailure(Failures, f"windowed project entry remains: {SourceLocation.relative_to(RepositoryRoot)}")


def CheckProjectZeroCopying(RepositoryRoot: Path, Failures: list[str]) -> None:
    for SourceLocation in sorted((RepositoryRoot / "Projects").glob("**/GameExecution.cpp")):
        RecordFailure(Failures, f"retired project host source remains: {SourceLocation.relative_to(RepositoryRoot)}")

    for SourceLocation in sorted(RepositoryRoot.glob("**/*")):
        if not SourceLocation.is_file() or SourceLocation.suffix not in {".cmake", ".ps1", ".sh", ".txt"}:
            continue
        if "PROJECT_ZERO_SOURCES" in ReadText(SourceLocation):
            RecordFailure(Failures, f"retired PROJECT_ZERO_SOURCES batch remains: {SourceLocation.relative_to(RepositoryRoot)}")


def CheckProjectSourceCrossing(RepositoryRoot: Path, Failures: list[str]) -> None:
    for SourceLocation in sorted((RepositoryRoot / "Projects").glob("**/*")):
        if not SourceLocation.is_file() or SourceLocation.suffix not in {".cpp", ".h", ".inl"}:
            continue

        ProjectFolder = next((Part for Part in SourceLocation.parts if Part.startswith("Project-")), None)
        if ProjectFolder is None:
            continue

        for LineNumber, SourceLine in enumerate(ReadText(SourceLocation).splitlines(), start=1):
            if "#include" not in SourceLine:
                continue
            ProjectSource = ProjectSourcePattern.search(SourceLine)
            if ProjectSource is not None and ProjectFolder not in ProjectSource.group(0):
                RecordFailure(
                    Failures,
                    f"cross-project source include at {SourceLocation.relative_to(RepositoryRoot)}:{LineNumber}",
                )


def ReadCodeInterchangeContract(RepositoryRoot: Path, Failures: list[str]) -> tuple[int, int] | None:
    HeaderLocation = RepositoryRoot / "Engine" / "ProjectInterchange" / "ProjectInterchange.h"
    if not HeaderLocation.is_file():
        RecordFailure(Failures, "Engine/ProjectInterchange/ProjectInterchange.h must declare the code-image contract")
        return None

    HeaderText = ReadText(HeaderLocation)
    NumberMatch = re.search(r"FrontierCodeInterchangeNumber\s*=\s*(\d+)u", HeaderText)
    FingerprintMatch = re.search(r"FrontierCodeInterchangeFingerprint\s*=\s*UINT64_C\((0x[0-9a-fA-F]+)\)", HeaderText)
    if NumberMatch is None or FingerprintMatch is None:
        RecordFailure(Failures, "ProjectInterchange.h must expose a numeric interchange number and UINT64_C fingerprint")
        return None

    return int(NumberMatch.group(1)), int(FingerprintMatch.group(1), 0)


def CheckProjectSpecifications(RepositoryRoot: Path, Failures: list[str]) -> None:
    ExpectedContract = ReadCodeInterchangeContract(RepositoryRoot, Failures)
    if ExpectedContract is None:
        return

    ExpectedNumber, ExpectedFingerprint = ExpectedContract
    for SpecificationLocation in sorted((RepositoryRoot / "Projects").glob("**/*.frontier")):
        Properties: dict[str, str] = {}
        ProjectSection = False
        for SourceLine in ReadText(SpecificationLocation).splitlines():
            ActiveLine = SourceLine.split("#", maxsplit=1)[0].strip()
            if ActiveLine == "[Project]":
                ProjectSection = True
                continue
            if ActiveLine.startswith("["):
                ProjectSection = False
                continue
            if not ProjectSection or "=" not in ActiveLine:
                continue
            PropertyName, PropertyText = ActiveLine.split("=", maxsplit=1)
            Properties[PropertyName.strip()] = PropertyText.strip().strip('"')

        ProjectName = Properties.get("ProjectName", "")
        RequiredProperties = {
            "ProjectFormatNumber",
            "ContentLocation",
            "OpeningScene",
            "CodeInterchangeNumber",
            "InterfaceFingerprint",
        }
        MissingProperties = sorted(RequiredProperties.difference(Properties))
        if not ProjectName or MissingProperties:
            RecordFailure(
                Failures,
                f"incomplete project specification {SpecificationLocation.relative_to(RepositoryRoot)}: "
                + ", ".join(MissingProperties),
            )
            continue

        try:
            ActualNumber = int(Properties["CodeInterchangeNumber"], 0)
            ActualFingerprint = int(Properties["InterfaceFingerprint"], 0)
        except ValueError:
            RecordFailure(
                Failures,
                f"invalid code-image contract values in {SpecificationLocation.relative_to(RepositoryRoot)}",
            )
            continue

        if (ActualNumber, ActualFingerprint) != (ExpectedNumber, ExpectedFingerprint):
            RecordFailure(
                Failures,
                f"code-image contract mismatch in {SpecificationLocation.relative_to(RepositoryRoot)}: "
                f"expected revision {ExpectedNumber}, fingerprint 0x{ExpectedFingerprint:016x}",
            )

        if ProjectName != "ProjectZero":
            for PropertyName in ("OpeningScene", "CodeImage"):
                if "Project-Zero" in Properties.get(PropertyName, ""):
                    RecordFailure(
                        Failures,
                        f"{SpecificationLocation.relative_to(RepositoryRoot)} resolves {PropertyName} through Project-Zero",
                    )


def CheckBuildOwnership(RepositoryRoot: Path, Failures: list[str]) -> None:
    CmakeLocation = RepositoryRoot / "CMakeLists.txt"
    PowerShellLocation = RepositoryRoot / "Tools" / "Build" / "ToolchainSequence.ps1"
    if not CmakeLocation.is_file() or "add_executable(Frontier ${FRONTIER_HOST_SOURCES})" not in ReadText(CmakeLocation):
        RecordFailure(Failures, "CMake must declare Frontier from FRONTIER_HOST_SOURCES")
    if not PowerShellLocation.is_file() or "Engine\\Host\\FrontierExecution.cpp" not in ReadText(PowerShellLocation):
        RecordFailure(Failures, "PowerShell must compile Engine\\Host\\FrontierExecution.cpp")
    if PowerShellLocation.is_file() and "Invoke-ProjectCodeImage" not in ReadText(PowerShellLocation):
        RecordFailure(Failures, "PowerShell must link isolated project code images")


def main() -> int:
    ArgumentParser = argparse.ArgumentParser(description="Verify Frontier project and host ownership boundaries.")
    ArgumentParser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    Arguments = ArgumentParser.parse_args()
    RepositoryRoot = Arguments.root.resolve()
    Failures: list[str] = []

    CheckWindowedEntries(RepositoryRoot, Failures)
    CheckProjectZeroCopying(RepositoryRoot, Failures)
    CheckProjectSourceCrossing(RepositoryRoot, Failures)
    CheckProjectSpecifications(RepositoryRoot, Failures)
    CheckBuildOwnership(RepositoryRoot, Failures)

    if Failures:
        print("Project ownership checks refused the following architecture:")
        for Explanation in Failures:
            print(f"  - {Explanation}")
        return 1

    print("Project ownership checks accepted the Frontier.exe / project-code-image boundaries.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
