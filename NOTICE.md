# Source and dependency notices

This repository consolidates the existing Frontier workspace with native sources from:

- **SultanAladin/Frontier-**, commit `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`.
- The workspace's previously developed native inspector, icon, Construct, automotive-material and billboard additions.
- The browser prototype and its existing selected **SultanAladin/Slate** icon imports; see `Experimental/FrontierEditor/vendor/slate-new-icons/NOTICE.md` for exact provenance and the original lack-of-licence notice.

`Docs/ConsolidationImport.json` records the native source import and the historical patches folded into it. Existing workspace files won conflicts; they were not silently overwritten with upstream versions. `Docs/Upstream/Frontier-README.md` preserves the imported source's original README. The original uploaded browser patch remains at `Tools/Legacy/UploadedBrowserPrototype.patch`.

No project-wide licence was present in the imported Frontier snapshot. This consolidation **does not invent a licence grant or assert that unlicensed upstream material is unrestricted**. The repository owner must determine the applicable permissions/licence before redistributing or relicensing it. Existing attribution and individual notices have been retained.

Third-party code is fetched from the public repositories/revisions listed in `ExternalPackages/Dependencies.lock.json`, not represented as original Frontier work. Each downloaded archive retains its licence files. Font licences are retained alongside the fonts in `EngineContent/FontArchives` and `EngineContent/Fonts`. Existing inspector/Lucide notices remain adjacent to their native implementations. Runtime celestial texture/star-data provenance is inherited from the upstream content and associated docs; no new ownership claim is made.

Build outputs, local scenes, credentials, downloaded dependency trees and large historical screenshot bundles are not part of the source consolidation.

Project-Fluid was imported from **SultanAladin/Frontier-**, branch `arena/01a0c575-frontier`, commit `6d230bb3406f7e574c07a8a4cc4df87942a914b7`. Only `Projects/Project-Fluid` was imported, not its older engine or exhibit tree. Its upstream research/provenance documents and [third-party notices](Projects/Project-Fluid/THIRD_PARTY_NOTICES.md) are retained. The Three.js lookup-table MIT notice does not license the entire project; the general upstream permission limitation above still applies. See [integration and local changes](Docs/WaterIntegration.md).
