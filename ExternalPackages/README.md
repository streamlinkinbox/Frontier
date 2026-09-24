# Managed third-party libraries

Run `python Tools/Bootstrap.py` from the repository root (or `python Tools/Setup.py --all-dependencies`). There are no nested git repositories or submodules to assemble manually.

`Dependencies.lock.json` specifies all 14 public archive URLs, immutable commit IDs, SHA-256 checksums and required source files. Five dependencies suffice for the native CPU proof; `--profile proof` installs only those. Other libraries support the application, physics and import paths. Revisions absent from the old upstream git tree were resolved explicitly during consolidation; this is recorded in each entry. Their full GPU/application compatibility is not yet execution-verified.

Setup verifies each archive **before extraction**, refuses links/traversal/special files, installs via staging, and refuses to overwrite an unmanaged or differently pinned directory. Archive caches are under `.cache/dependency-archives/`. `--check` works offline and checks installation identities, source witnesses and ImGui patch sentinels; it is not a complete post-install file-integrity scan.

Downloaded source trees and generated dependency libraries are ignored by Git. They retain their own copyright/licence files. TinyBVH's archive includes a large example dataset: only its required root headers and notices are installed (the full archive download is still about 203 MB). The archive checksum covers that entire download.

Only the existing third-party ImGui tab/docking modifications are applied at setup. **All Frontier engine/editor changes already live in ordinary committed source files.** The source-level ImGui divergence remains under `Tools/Build/Patches/Patch*.patch`; ThorVG's narrow allocation compatibility change is staged into the build directory, not written into the downloaded source.

To upgrade a dependency, review compatibility/licences, update both revision and checksum in the lockfile, move the old downloaded package aside, and rerun setup and native tests. Never disable TLS verification to work around a network failure.
