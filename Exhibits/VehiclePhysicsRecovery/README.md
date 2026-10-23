# Vehicle Physics Recovery — XPBD Build/Run Evidence

## What this proves

- ✔️ The final native CPU XPBD tyre (`SultanAladin/Frontier-@38d28c3`, pre-removal) compiles clean
  and runs headless today: **10 passed, 4 failed** against its own Pacejka cross-validation.
- ✔️ The older Slate-lineage revision (`ca73246`) scores 9/14 under the identical validation file,
  so `38d28c3` is both the latest and the best starting point. Logs: `Results/`.
- 🔴 Not proven: calibration within oracle bands (4 fails listed in the plan §3), vehicle-level
  driving checks, Windows/MSVC, Vulkan/ray tracing, ec6's reported 7/7 CTest suite (not re-run here).

## Reproduce (exact)

- Host: Linux sandbox, `g++ (Debian 12.2.0)`, 2 cores. Not a Windows desktop session.
- Sources are read from the pinned repositories at run time; no copies are retained here
  (exhibits reuse production sources, per `CLAUDE.md` folder rules).

```sh
git clone --filter=blob:none --no-checkout https://github.com/SultanAladin/Frontier-.git src
cd src
for f in Vehicle/XPBDSoftTyre.h Vehicle/XPBDSoftTyre.cpp Vehicle/XPBDTyreValidation.cpp \
         Vehicle/PacejkaMagicFormula.h Vehicle/PacejkaMagicFormula.cpp; do
  git show 38d28c3:Engine/PhysicalDynamics/$f > $(basename $f)
done
g++ -std=c++17 -O2 XPBDSoftTyre.cpp PacejkaMagicFormula.cpp XPBDTyreValidation.cpp -o xpbdval
./xpbdval   # expect: RESULT: 10 passed, 4 failed   (~10 s)
```

## Results table

| Revision | Build | Stability/load checks | Fx/Fy shape checks | Stiffness + Mz checks | Total |
| --- | --- | --- | --- | --- | --- |
| `38d28c3` (final) | ✔️ clean | ✔️ 5/5 | ✔️ signs + peaks plausible | 🔴 1.32× peak, 4.2×/2.6× stiffness, Mz sign | 10/14 |
| `ca73246` (Slate lineage) | ✔️ clean | 🚩 free-roll residual fail | ✔️ signs | 🔴 same family, worse | 9/14 |
