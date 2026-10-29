# Projectspace Preference Persistence

Implemented editor project configuration as one editable `.projectspace` container. No project preference sidecars.
The encoding is UTF-8 TOML through the existing toml++ dependency, with an explicit editor-format discriminator.
It is not binary FSPC and is not presented as compatibility with the older runtime reader.

## Files and use

- Installed blank template: `EngineContent/Defaults/BlankProject/Blank.projectspace`.
- Shared codec: `Engine/ContentInterchange/ProjectSpaceCodec.{h,cpp}`.
- Each project owns its `.projectspace` file, including project identity, content roots, level references, catalogue,
  dependency-pin metadata, engine configuration, appearance, input and notification defaults.
- The Windows host uses `%LOCALAPPDATA%/Frontier/Personal.projectspace` for optional personal settings.
- Personal/Project chips explicitly choose the save destination. Personal overrides have an explicit On/Off control.
- A personal save includes only explicitly saved profile sections, not a duplicate of the entire project's defaults.
- Project settings are defaults; enabled personal profiles take precedence. Disabling them restores project settings.
- Drafts must be applied/discarded before profile reload or override-precedence changes.
- Installed engine defaults are read-only to the editor preference-saving path.

```text
FrontierUI.exe --create-project "C:\Projects\Example\Example.projectspace"
FrontierUI.exe --project "C:\Projects\Example\Example.projectspace"
```

Creation validates/snapshots the template, issues a fresh project identity, creates content roots, and publishes the
new file without overwriting an existing one. It requires filesystem hard-link support for no-replacement publication.
Without a project argument, the host uses engine defaults plus personal preferences. No project picker is implemented.

## Persistence and fallback

Appearance/Input standalone compatibility codecs now expose shared table operations. The embedded profiles reuse
those same validators/encoders rather than independent implementations. Existing explicit legacy TOML callers still
work; the Windows host's new path and all new project saves use `.projectspace`.
Existing legacy personal TOML files are not automatically migrated or deleted in this slice.

Missing project profile sections inherit engine defaults. Missing requested fonts use an installed fallback without
changing the stored requested family name. With no usable font files, the UI can start with ImGui's built-in emergency
face; this does not claim complete Unicode coverage. Invalid project containers are refused rather than overwritten.
Missing/corrupt engine defaults fall back to built-in preference structures.

Saves replace the file only after validation and complete temporary-file writing. Live applied preferences change only
on successful save. Each save rereads the destination to preserve unrelated project sections and optional extension
keys. TOML comments/formatting and extension keys inside normalized shortcut-array rows are not preserved.
Concurrent writers, hostile filesystem mutation and power-loss durability are not guaranteed; there is no project lock.
Failed creation can leave empty newly created directories, but cannot replace an existing project file.

## Verification

```sh
python Exhibits/ControlCentre/Code/ValidateNavigation.py
```

The driver builds/runs eleven tests in Debug and address/undefined-behaviour-sanitizer configurations, regenerates
native
CPU captures and cross-builds/packages/inspects Windows x64. See `../ControlCentre/Results/NavigationChecks.json`.
`Code/ProjectSpaceValidation.cpp` exercises fresh identity, no overwrite, Unicode paths, version/size/type refusal,
root-locator validation, malformed pins, missing profiles/fonts, dirty drafts, saved notifications, independent
personal/project saves, override precedence, extension/catalogue retention and failed-write preservation.
It feeds actual native pointer input to the scope/Apply controls and emits four production draw-command captures.

## Not implemented by this slice

- Genuine notification action callbacks and independent severe-error presentation.
- Binary runtime packaging/Space codecs, payload embedding, dependency resolution/digest verification or scene loading.
- Level editing, automatic project switching, a project picker or a multi-writer project lock.
- Windows runtime, GPU, DPI/device-loss execution. Cross-compilation is not Windows runtime verification.

A subsequent authorised slice adds top-right notification cards and session history; see `../Notifications/README.md`.
Notification preferences still persist in projectspace; transient messages/history are not serialized.
