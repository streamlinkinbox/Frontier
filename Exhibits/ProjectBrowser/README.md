# Native Project Browser conversion

## Current slice

- `UI/Editor/ProjectBrowserPanel.{h,cpp}` owns the native ImGui browser.
- The existing Windows GLFW host starts in the browser when no project argument is supplied.
- `--project` and `--create-project` remain supported and validate the project before editor startup.
- The only creation preset is **Blank Project**, using `EngineContent/Defaults/BlankProject/Blank.projectspace`.
- Real project files and folders are enumerated through the existing shared `ContentDirectory` and presented as a responsive native card grid; no mock sample files or preview renders are injected.
- Native Windows open/save dialogs select project containers; typed folder navigation accepts arbitrary folders.
- Selection decodes the real container and shows its name. Editor hand-off revalidates the file before activation.
- Creation snapshots the existing project defaults, issues a fresh identity and refuses overwrites.
- Three columns retain shared dimensions, aligned 64 px headers, collapsible sides and narrow-screen collapse.
- The host supplies the existing shared ControlKit palette and typography; no independent theme files are introduced.
- The six HTML rendering fixtures remain design-reference material only. They are not native presets or projects.

## Validation

`Code/ProjectBrowserValidation.cpp` is registered as the `ProjectBrowserValidation` CMake/CTest target.
It checks blank-only presets, Unicode locations, real creation, fresh identity, empty content, preference snapshots,
overwrite refusal, unsupported/missing/corrupt project refusal, revalidation before launch, one-time hand-off,
read-only browsing and actual desktop/narrow ImGui commands and column/header geometry.
Each run creates a fresh scratch subdirectory without deleting any existing project.

The bounded host check compiles only the browser, existing directory/project/preference codecs and pinned ImGui.
It does not build ThorVG, a scene renderer, native packages or the full editor. Result:
`Results/NativeBrowserChecks.json`. This is host CPU execution, not Windows, OS-dialog or GPU execution.
The new translation units also pass `-Wall -Wextra -Werror` syntax checks.

## Remaining conversion

- Port the actual reference scene/thumbnail rendering into the native Vulkan/Slang path; no fake snapshots.
- Port the A4 Markdown viewer, zoom, inspector tabs and safe document interactions.
- Connect actual project/CAD/geometry formats and real version history only as their codecs become available.
- Finish visual parity, shared-control styling, native folder dialogs and return-to-browser navigation.
- Compile and execute the Windows host; exercise native dialogs, editor transition, fonts, DPI and GPU rendering.
- Review executable naming/packaging for the final Frontier.exe startup route.

Port 8770 continues to show the HTML reference, not a C++ application running in a web browser.
