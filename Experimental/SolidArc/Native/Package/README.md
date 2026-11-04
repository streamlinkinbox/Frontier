# 🧩 SolidArc — Windows x64 preview package

**TL;DR** — Unzip anywhere and start `SolidArcWorkspace.exe`. It needs a GPU driver with Vulkan 1.3 (current NVIDIA, AMD and Intel drivers install `vulkan-1.dll`). Nothing gets installed: the options, autosave and saved documents stay in this folder.

| Item | Where |
|---|---|
| Application | `SolidArcWorkspace.exe` |
| Options (window, viewport, font, file paths, Vulkan) | `SolidArcWorkspace.toml` |
| Autosave (written 0.6 s after the last edit) | `Autosave/SolidArcWorkspace.solidarc` |
| Default Save / Open document | `Documents/Bracket.solidarc` |
| Import curves sample | `Samples/SyntheticBonnet.toml` |
| UI font and icons | `EngineContent/` |
| Third-party licences | `Notices/` |
| **Log** (every command, preview, kernel step, refusal and slow frame) | `SolidArcTrace.log` (the run before: `SolidArcTrace.previous.log`) |

## 💡 Start

- Double-click `SolidArcWorkspace.exe`, or open a document: `SolidArcWorkspace.exe "C:\Parts\Bracket.solidarc"`
  (`--open` before the path works too).
- To open `.solidarc` files with a double-click, choose **Open with → Look for another app on this PC**, pick
  `SolidArcWorkspace.exe` and tick **Always use this app**. Windows passes the file as a bare path, which opens it.
- From Frontier: **Project browser → New → SolidArc model** creates an empty model and opens it here; **Open from
  disk** lists `.solidarc` files, and **Open in SolidArc** starts the workspace with the chosen one.
- `--specimen` starts with the fillet / chamfer specimen; `--options <file>` uses another options file.
- Navigation and commands follow Plasticity (orbit, pan and zoom on the mouse; `B` fillet, `E` extrude, `1`–`5` modes, `Alt+Z` x-ray).
- On a curved face, a sketch lies on the exact tangent plane under the pointer. `E` → Union starts the extrude slightly inside the face (the Inspector shows how far), so the joined body closes and its junction edge takes a chamfer.
- A slow fillet / chamfer preview is rebuilt when the pointer rests, not on every pixel. A typed size applies at once.
- **Right-click or `Enter` confirms** every command (fillet / chamfer, extrude, inset, move / rotate / scale, offset, pipe, joint, boolean, a pending dimension) and ends Trim / Fill; `Esc` cancels. A right-drag still pans. A size typed in the field and not yet entered is the one a right-click applies.
- **Knife (`Shift+K`, or the knife icon at the right end of the viewport toolbar)**, in the style of Blender KitOps. The Inspector card switches between three modes (`Tab`):
    - **Sketch**: draw a circle / rectangle / polygon / slot / line on the face under the pointer, then move sideways for the depth or type it. `T` cuts straight through.
    - **Insert**: pick a cutter from the boolean library (7 built-ins plus every `.step` in `Inserts/`), point at a face and click to place it. Move to size it or type a width; `[` / `]` turns it 15°. **Save picked body** adds a body to the library.
    - **Object**: pick the target, then Shift-pick the cutter bodies. `K` keeps the cutters.
    - `Q` add · `W` cut · `E` intersect · `R` separate. Each knife is one undo entry.
- Edges are soft-edged (antialiased) strokes held a few pixels in front of their faces at every zoom, so curved edges no longer break into dashes. The viewport renders with 4× multisampling (the status bar shows `4× MSAA` next to the GPU name), so face silhouettes are smooth too.

## 🔍 Log

- `SolidArcTrace.log` sits next to `SolidArcWorkspace.exe` and is rewritten on each start (the previous run is kept as `SolidArcTrace.previous.log`).
- In the app: the **Log** button at the bottom right of the status bar (or `F12`) opens the log panel. It shows the file path (Copy path), filters (Slow frames, Refusals) and Copy.
- A frame over 50 ms writes a `Frame` line: the total, the interface / upload / GPU wait split, the open session and the costliest phases (for example `bevel preview · stage 39 ms`). The button reads `Log · N slow` once that has happened.
- When something is slow or refused, send `SolidArcTrace.log` (or the panel's Copy).

## ⚠️ Preview limits

- Built on Linux with Zig's clang for `x86_64-windows-gnu` (MinGW-w64, static C++ runtime). The MSVC build of the same sources is the release build.
- Not yet started on a Windows machine by the build: please report the first launch (window, viewport, opening a document).
- If the window does not open, a message box names the cause (no Vulkan 1.3 driver, missing `EngineContent/`, unreadable options file).
- Unsigned preview: Windows SmartScreen may ask for confirmation on the first start (More info → Run anyway).
- Antivirus: an unsigned cross-built executable can trip Microsoft Defender's machine-learning heuristics (detections ending in `!ml`), and the browser then deletes the zip as "Virus detected". If a build is flagged, compare its sha256 with the published one (`Get-FileHash SolidArc-Windows-x64.zip`) then report it at https://www.microsoft.com/en-us/wdsi/filesubmission ("Incorrectly detected"), keep the builds in one folder excluded from scanning, or build with MSVC. "Allow on device" in Protection history allows that detection name for every file, so it is the widest choice.
