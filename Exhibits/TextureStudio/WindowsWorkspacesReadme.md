# Windows standalone application tests

Extract the complete archive before launching. Windows 10/11 x64 and an installed Vulkan 1.2-capable GPU driver
are required. Shader binaries are included; no compiler or Vulkan SDK installation is required on the test machine.

| Executable | Scope |
|---|---|
| `Frontier.exe` | Project Browser only. Create/open/select validates projects but deliberately stays in the browser. |
| `TextureWorkspace.exe` | Material Library, Preview, Inspector and an experimental native Pattern workspace. Stamp and baking remain deferred. |

Launch `Frontier.exe` or `TextureWorkspace.exe` directly. No command or PowerShell launch scripts are bundled.
Both resolve bundled engine content relative to their executable. Materials are copied to
`%LOCALAPPDATA%/Frontier/TextureStudio/Materials` before editing so the packaged presets remain unchanged.
Existing personal materials are preserved, not overwritten by an updated test archive.

## Acceptance checklist

- Launch both applications. Frontier must never enter the general editor/CAD workspace.
- In Texture Workspace select Racing Green and another material; allow the first shader-ball render to complete.
- Use minus / percentage reset / plus, or the wheel over the specimen canvas. Click **Fullscreen** for a fitted full-work-area specimen; **Fit object** and Escape are available there.
- Left-drag to orbit; middle-drag or Shift-left-drag to pan; right-drag vertically to zoom. The selector menus use opaque panels.
- Reset View, F, Home or double-click recentres/refits and resets orbit. Arrow keys pan while hovering the canvas.
- Test that release stops dragging, sliders/popups do not move the camera, and shared sensitivity/invert-pitch
  preferences affect orbit. Geometry, lighting and exposure stay unchanged when only the view is reset.
- Switch Shader Ball, Sphere, Plane and Cube. Switch Studio Softbox, Warm Courtyard, Cool Rim and Overcast.
- Change Exposure. Only the large preview should change; catalogue thumbnails keep their fixed lighting/geometry.
- Edit a supported coefficient, Apply, Revert and Save. Invalid source must preserve the last valid preview.
- In the experimental Pattern workspace, author a simple shape, set different Fill and Stroke colours, and inspect
  the Colour/Normal/Height-derived maps. Switch back to Material after GPU readback and verify the selected-material
  preview updates after changing the Pattern. This is preview-only: do not treat it as a saved material or scene
  assignment. The independent material-application validation gate is still open.
- Restart, resize, collapse panels and test a second monitor only if that preference is explicitly enabled.

## Diagnostic collection

Logs: `%LOCALAPPDATA%/Frontier/Logs/Frontier-*.log` and `TextureWorkspace-*.log`.
If LOCALAPPDATA is unavailable, look in `%TEMP%/Frontier/Logs`. Files are flushed after events, capped at 8 MiB
per run, and retain the latest eight runs per application. Startup paths, font/icon recovery, Vulkan device/driver,
shader byte counts, material operations, preview settings, GPU submissions and severe failures are recorded. In a
Debug preview, Pattern Float32 bridge events are tagged `PatternPreviewDebug`: bridge setup, document counts,
recorded/confirmed submission serials, completed readback, and whether maps were accepted or discarded as stale.
The trace contains no Pattern source. Release builds omit these per-event debug traces.

Copy the newest log files from that folder using File Explorer. Send them with `Package.json`, your GPU/Windows
version, reproduction steps and a screenshot of any error.
Logs contain local file paths but not document source or credentials; review them before sharing.
A native severe-failure dialog remains available when normal rendering fails. Hard termination/power loss cannot
be guaranteed to generate a final entry; compile/package success is not user GPU-driver/startup certification.

## Provenance and limits

`Package.json` records the source revision, payload SHA-256 and shader/source fingerprints. All ten authored Slang
entry points are compiled by CI; runtime binaries reside in `EngineContent/ShaderArchives/Surface`.
The CI CPU/native UI tests do not create an interactive Windows Vulkan window. Runtime GPU/driver acceptance
still requires the checklist above. Seven automotive procedural recipes are implemented; remaining materials
retain the previously documented supported physical subset. Exact reference appearance is not yet certified.


## Download trust

Earlier test downloads are superseded; a browser warning is not evidence that a download is safe or malicious.
Do not disable Chrome Safe Browsing, antivirus or Windows protection to run a test build.
Binary distribution requires a current clean local Defender scan. Publisher signatures must be valid unless an
unsigned preview is explicitly authorized in CI and packaging. Invalid or untrusted signatures remain blocked.
An unsigned preview is labelled in the release title, `UNSIGNED-PREVIEW.txt` and the packaged signature report.
Unavailable or failed scanning withholds binary uploads; checksums alone do not establish safety.
Even signed, scanned software can receive reputation warnings. Report the exact Chrome warning for diagnosis.


### Direct GitHub delivery

Verified packages can be published using **Windows standalone workspaces → Run workflow → publish_release**.
This is opt-in: ordinary branch pushes never publish a release. Publication requires the authorized trust checks,
then rechecks the archive checksum before uploading the original ZIP and its checksum to a GitHub prerelease.
Download from that release page, not an Arena-rehosted copy. This matches reference-project provenance;
it is not a method for clearing or bypassing a dangerous-download verdict. A blocked package stays withheld.
Signature and antivirus statuses are also shown in the Actions summary and annotations, not just the report.

Unsigned test artifacts require **allow_unsigned_preview** when dispatching; select **debug_preview** to include
Pattern bridge traces. On this branch, a deliberate push whose commit message contains both
`[debug-preview]` and `[publish-windows-unsigned-preview]` requests a Debug trace build and its scanned, clearly
labelled Actions artifact. Debug packages are never published as GitHub releases. A non-Debug release publication
still requires the separate **publish_release** input (or the documented explicit push publication policy).
The normal/default policy stays signed-only. No scan is skipped under either policy.
