/*====================================================================================================================================
                                                       SOLIDARCBROWSERLINK.H
====================================================================================================================================*/
// 🧩 Connects the Frontier project browser to SolidArc: native .solidarc previews and the workspace launch

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/Editor/ProjectBrowserPanel.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// Starts Workspace with Arguments followed by "--open <Document>"; false + Refusal when the process could not start.
using SolidArcLaunchRoutine = std::function<bool(const std::filesystem::path& Document, std::string& Refusal)>;

struct SolidArcBrowserLinkSettings
{
    std::filesystem::path    Workspace = {};                                                 // [-] - SolidArcWorkspace executable
    std::vector<std::string> Arguments = {};                                                 // [-] - Leading arguments (before --open)
    SolidArcLaunchRoutine    Launch    = {};                                                 // [-] - Replacement launch (empty: spawn Workspace)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 SolidArcWorkspace(.exe) beside the running executable (the build places every application in one directory).
[[nodiscard]] std::filesystem::path LocateSolidArcWorkspace();

// Spawns a detached process (CreateProcessW on Windows, fork + exec elsewhere); the current directory is inherited so the
// workspace resolves EngineContent exactly like the browser. Exec failures in the child are reported, not swallowed.
bool LaunchSolidArcWorkspace(const std::filesystem::path& Workspace, const std::vector<std::string>& Arguments,
                             const std::filesystem::path& Document, std::string& Refusal);

// Browser-side description of one document (geometry, residence key, summary, refusal).
[[nodiscard]] BrowserNativeDocument DescribeSolidArcBrowserDocument(const std::filesystem::path& Path);

// 📝 The browser's New dialog › SolidArc model: an empty document titled Title (empty: the file name), written by SolidArc's
//    own codec so the workspace opens it like any saved model. False + Refusal when Path exists or cannot be written.
bool CreateSolidArcBrowserDocument(const std::filesystem::path& Path, const std::string& Title, std::string& Refusal);

// Installs QueryDocumentGeometry, CreateWorkspaceDocument and LaunchWorkspaceDocument on Browser. QueryConfiguredThumbnail
// stays with the host, which owns the preview renderer.
void ConnectSolidArcBrowser(ProjectBrowserPanel& Browser, SolidArcBrowserLinkSettings Settings);

} // namespace Frontier::SolidArc
