# Building Frontier and recovering external packages

Run commands **from the repository root**. This is the current consolidated build route; old submodule/reconstruction instructions in historical documents are not required.

## 1. Tools and projects

| Target | Required tools | Dependency setup |
|---|---|---|
| Project-Zero, Windows | Python 3.11+, Git, VS 2022+ Desktop C++ workload (x64), Windows SDK, CMake 3.21+, Ninja, Vulkan SDK and graphics driver | `python Tools/Bootstrap.py --profile all` |
| Project-Zero, Linux | Python 3.11+, Git, CMake 3.21+, Ninja, C++20 compiler, Vulkan SDK/shader compiler, GLFW window-system development packages | `python3 Tools/Bootstrap.py --profile all` |
| Native editor CPU proof | Python 3.11+, Git, CMake 3.21+, Ninja, C++20 compiler; no GPU required | `python Tools/Bootstrap.py --profile proof` |
| Project-Dyno (audio) | Python 3.11+, C++20 compiler; MSVC on Windows, g++/clang++ on Linux | `python Tools/Bootstrap.py --package miniaudio` |
| Project-Fluid CPU tests | CMake 3.21+, Ninja, C++20 compiler | No external download for the CPU preset |
| SolidArc standalone | CMake and C++20 compiler | See commands below |
| Browser experiment | Node.js 20.19+ and npm | `npm --prefix Experimental/FrontierEditor ci` |

**Installed source != built library.** A downloaded GLFW, ThorVG or Jolt folder contains source. Project-Zero must also compile its `.lib`/DLL outputs. Re-downloading source does not fix a missing compiler, stale CMake cache, incompatible ISA or linker configuration.

### Project-Zero / Windows

Use an **x64 Native Tools / Developer PowerShell** terminal. Check `python --version`, `git --version`, `cmake --version`, `ninja --version`, `cl`, and `$env:VULKAN_SDK`.

```powershell
# Install once; valid installed packages are reused, not downloaded again.
python Tools/Bootstrap.py --profile all
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed' }

# Build only: checks local dependencies, does not download them.
powershell -NoProfile -ExecutionPolicy Bypass -File Projects/Project-Zero/Build/ToolchainSequence.ps1 -Development -Run

# After updating engine headers/shaders, a full application rebuild:
powershell -NoProfile -ExecutionPolicy Bypass -File Projects/Project-Zero/Build/ToolchainSequence.ps1 -Rebuild -Development -Run
```

The driver builds GLFW/ThorVG and builds Jolt when its archive is absent. `-Configuration Debug` selects Debug; `-Isa SSE2` is the default portable x64 instruction set. If switching ISA or replacing Jolt source, explicitly rebuild Jolt **with the same configuration and ISA** before rebuilding Project-Zero:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Build/BuildJolt.ps1 -Rebuild -Configuration Release -Isa SSE2
```

`-Rebuild` on Project-Zero is not a request to redownload dependencies, nor a guarantee to rebuild an existing Jolt archive. `-SetupDependencies` is the explicit opt-in to install missing dependencies during a Windows build; prefer the separate setup command when troubleshooting.

### Project-Zero / Linux

```sh
python3 Tools/Bootstrap.py --profile all
cmake --preset linux-app
cmake --build --preset linux-app
```

CMake validates dependency installations offline. GLFW still requires your distribution's window-system development packages. The full Windows/Vulkan and Linux Vulkan application have not been runtime-verified in this environment; a CPU or dependency test is not a substitute.

### Native editor CPU proof

```sh
python Tools/Setup.py --verify
# Or once sources are installed:
python Tools/Setup.py --offline --verify
```

Evidence is generated in `build/native-proof/evidence/`. The offline option concerns dependency setup; the compiler/CMake/Ninja tools must already be installed. `--offline --browser` is refused because npm may access the network.

### Project-Dyno

```powershell
python Tools/Bootstrap.py --package miniaudio
powershell -NoProfile -ExecutionPolicy Bypass -File Projects/Project-Dyno/Build/ToolchainSequence.ps1 -Rebuild -Run
```

```sh
python3 Tools/Bootstrap.py --package miniaudio
bash Projects/Project-Dyno/Build/ToolchainSequence.sh
```

No Vulkan, ImGui, ThorVG or GLFW installation is needed for this project. Linux output: `Projects/Project-Dyno/Build/Output/Linux/Release/Binary/Project-Dyno`. The Linux build was executed successfully when these instructions were added; Windows execution remains untested here.

### Project-Fluid and SolidArc

```sh
cmake --preset fluid-cpu
cmake --build --preset fluid-cpu
ctest --preset fluid-cpu

cmake -S Editor/AuthoringTools/Modelling/SolidArc -B build/solidarc
cmake --build build/solidarc --target SolidArc SolidArcVerification --parallel 3
ctest --test-dir build/solidarc --output-on-failure
```

GPU fluid is a separate path: [Fluid GPU testing](FluidGpuTesting.md).

### Browser experiment

```sh
npm --prefix Experimental/FrontierEditor ci
npm --prefix Experimental/FrontierEditor run dev -- --port 5173
# Production bundle:
npm --prefix Experimental/FrontierEditor run build
```

## 2. Updating the codebase without losing downloaded packages

Keep **both** `ExternalPackages/` and `.cache/dependency-archives/` when updating source. Both are ignored/generated, so a fresh clone, clean ZIP or `git clean -xfd` will not preserve them automatically. Avoid that clean command unless you intend to erase dependencies and local authoring data.

1. Update the source, including `ExternalPackages/Dependencies.lock.json`.
2. Run `python Tools/Bootstrap.py --check`. It makes **no network requests** and reports every missing/incomplete/wrong-pin package in the selected set.
3. If it says `Ready`, leave those packages alone. No archive is needed to reuse a healthy installation.
4. Install only missing packages with `--package NAME`. If the lock changed or a managed install is damaged, repair **only that package**, below.
5. Rebuild native outputs; clear a stale *generated build directory*, not the downloaded source tree.

A package is identified by its `.frontier-dependency.json` marker, locked revision/SHA-256 and required source witness. Preserve the marker when copying installed dependencies. `--check` is a quick identity/witness check, **not** a complete source-integrity scan or ABI/linker test. It also verifies required ImGui patches when ImGui is selected.

## 3. Repair just one package — no redownload when the archive is cached

Example using ImGui; substitute the exact package name from the lockfile:

```powershell
python Tools/Bootstrap.py --package imgui --check
python Tools/Bootstrap.py --package imgui --repair --offline
python Tools/Bootstrap.py --package imgui --check
```

Repair verifies and extracts the archive **before** replacing any installed files. It preserves the old managed directory in `ExternalPackages/.frontier-backups/NAME-TIMESTAMP/`. A download, checksum or extraction failure leaves the old installation intact. ImGui's third-party patches are reapplied after installation; if patch application fails, setup fails and the old tree remains available in the backup.

`--repair` requires explicit `--package` selection; it cannot accidentally reinstall all 14 packages. It also works for a managed directory with a damaged marker or missing witness. It does **not** overwrite an unmanaged hand-downloaded folder: move that folder aside yourself and run setup. Do not fabricate a marker to bless an unknown version.

If an offline archive is missing, setup prints its exact expected path and stops without trying the network. To permit a download for that package:

```powershell
python Tools/Bootstrap.py --package imgui --repair
```

A checksum failure does not trigger deletion of the installed package. Quarantine only the archive named in the error, then obtain it again. Do not delete the entire cache or every external package.

## 4. Keep a shared cache across checkouts

```powershell
$env:FRONTIER_DEPENDENCY_CACHE = 'D:\FrontierCache\archives'
python Tools/Bootstrap.py --profile all
# Another checkout, using the same cache:
python Tools/Bootstrap.py --profile all --offline
```

Or pass `--cache-dir D:\FrontierCache\archives` directly to Bootstrap/Setup. This selects **one** archive directory, not a search list; move existing cached archives there if needed. Already-installed valid packages are used regardless of archive location.

For an air-gapped machine, copy verified archives from `.cache/dependency-archives/` (or the shared cache). Filenames are `NAME-REVISION.tar.gz`; exact URLs, revisions and SHA-256 values are in `ExternalPackages/Dependencies.lock.json`. Keep the GitHub `.tar.gz` archive intact instead of substituting an extracted folder or `.zip`. Bootstrap verifies its checksum before extraction.

## 5. HTTPS, proxy and curl recovery — without insecure SSL switches

Normal setup tries Python HTTPS with up to three attempts, then system `curl` with up to three attempts if available. Every successful transport must still match the **locked SHA-256**. A checksum mismatch stops rather than trying another transport.

To choose curl explicitly (the script launches the executable, not PowerShell's historical `curl` alias):

```powershell
python Tools/Bootstrap.py --package miniaudio --downloader curl
# Include --repair only if replacing a managed installation.
```

Both paths validate TLS certificates. Curl permits only HTTPS URLs and redirects. **Do not use `curl -k`, `--insecure`, `GIT_SSL_NO_VERIFY`, or disable certificate verification.**

For corporate TLS inspection, obtain the trusted PEM CA bundle from your administrator, then:

```powershell
python Tools/Bootstrap.py --package miniaudio --downloader curl --ca-file C:\Certificates\company-ca-bundle.pem
```

`--ca-file` applies to both Python and curl. Standard proxy environment settings such as `HTTPS_PROXY` are respected by the underlying tools. Do not paste credential-bearing proxy URLs into logs or bug reports. Check the system clock, certificate store, firewall/proxy policy and reachability of `codeload.github.com`. A GitHub login or token is not needed for these public archives. If that host is blocked entirely, use the shared/offline cache route instead of repeatedly deleting and downloading.

## 6. Distinguish source, patch, build and network failures

| Error | Action |
|---|---|
| Missing/wrong pin/incomplete package | `--check`, then targeted install or `--repair --offline`; inspect the printed path |
| Folder exists but is unmanaged | Preserve/move that folder; install the locked archive. Do not delete other packages |
| Missing ImGui custom members / patch check fails | Repair only ImGui from cache; do not manually apply obsolete engine patches |
| Missing `glfw3dll.lib`, `glfw3.dll`, `thorvg.lib` | Check the native dependency compilation log, CMake/Ninja/MSVC availability; downloading headers alone cannot create libraries |
| Jolt ISA/configuration mismatch | Rebuild Jolt with matching `-Isa` and `-Configuration`, then rebuild Project-Zero |
| CMake references an old source path/compiler/generator | Rename the affected generated directory, e.g. `build/dependencies-Release`, then rebuild; preserve `ExternalPackages/` and the archive cache |
| Vulkan SDK / shader compiler absent | Install/fix `VULKAN_SDK` and SDK tools. Downloaded `vulkan-headers` is **not** the Vulkan SDK |
| Checksum mismatch | Quarantine only the named archive; keep the lockfile checksum unchanged |
| TLS/network error | Try verified curl or a trusted CA bundle; otherwise copy the locked archive into the offline cache |

Capture the **first error**, not only the final linker failure:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Projects/Project-Zero/Build/ToolchainSequence.ps1 -Rebuild 2>&1 | Tee-Object build-project-zero.log
```

Include your build command, Python/compiler/CMake versions, `Bootstrap.py --check` output, and the first failing compile/link/setup message. Redact private paths or proxy credentials. Without that output, we cannot establish which of these caused your specific failure.

## Checks for the dependency tooling

```sh
python Tools/Tests/TestBootstrap.py
```

15 tests cover archive safety, checksum rejection, offline cache installation, installed-package reuse with no cache, offline misses with no network, targeted repair/backup, failed-repair preservation, malformed/wrong-pin markers and mocked verified curl fallback and HTTP redirect rejection. Python/shell syntax checks and a real Linux Project-Dyno build passed. Windows/MSVC and corporate-network recovery were not executed here.
