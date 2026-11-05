#!/bin/bash
#============================================================================================================================================
#                                                        MUTATEPLUMESHADERS.SH
#============================================================================================================================================
# 🧩 Mutation check of the plume parity: each mutation edits a COPY of one Slang unit (the repository is untouched), recompiles every entry that
#    includes it into a copy of the archive folder, and runs the parity against the reference RunPlumeParity.sh left. A mutant the parity does not
#    flag as "not bit for bit" survived: it is equivalent in the scenario, or the scenario does not reach it (the README says which).
#
#        Exhibits/PlumeSimulation/Code/MutatePlumeShaders.sh <build folder>
set -u
R=$(cd "$(dirname "$0")/../../.." && pwd)
M=${PLUME_MUTATION_SCRATCH:-$R/_ClaudeScratch/mut}
Build=${1:?usage: MutatePlumeShaders.sh <build folder>}
Build=$(cd "$Build" && pwd)
SS=$R/ExternalPackages/VulkanCompute/SwiftShader
SLANGDIR=$R/ExternalPackages/VulkanCompute/Slang/slangtorch/bin
export LD_LIBRARY_PATH=$SS FRONTIER_VULKAN_DRIVER=$SS/libvulkan.so.1 VK_ICD_FILENAMES=$SS/vk_swiftshader_icd.json
REF=${REF:-$R/_ClaudeScratch/plume/reference-low-170}
run() { # name unit sed-expression
  local Name=$1 Unit=$2 Expr=$3
  rm -rf $M/slang_$Name $M/spv_$Name; mkdir -p $M/spv_$Name
  cp -r $R/Frontier/Engine/Shaders/PlumeSimulation $M/slang_$Name
  cp $Build/ShaderArchives/PlumeSimulation/*.spv $M/spv_$Name/
  local File=$M/slang_$Name/$Unit.slang
  [ -f $File ] || File=$M/slang_$Name/PlumeLayouts.slang
  cp $File $File.orig
  sed -i "$Expr" $File
  if cmp -s $File $File.orig; then echo "MUTATION $Name: NO CHANGE (pattern missing)"; return; fi
  # the units that include the mutated file all recompile
  for U in PlumeAdvection PlumeVorticity PlumePressure PlumeMarch; do
    for E in $(grep -o "\"$U:[A-Za-z]*:compute\"" $R/Automation/PlumeSimulationRendering.cmake | sed 's/"//g' | cut -d: -f2); do
      $SLANGDIR/slangc $M/slang_$Name/$U.slang -I $M/slang_$Name -I $R/Frontier/Engine/Shaders/GlobalIllumination -entry $E -stage compute -target spirv -profile spirv_1_3 -emit-spirv-directly -matrix-layout-row-major -fvk-use-entrypoint-name -O0 -o $M/spv_$Name/$U.$E.spv > $M/spv_$Name/$U.$E.log 2>&1 || echo "  compile failure $U.$E"
    done
  done
  local Out; Out=$($Build/PlumeSimulationCapture parity $R $M/spv_$Name $REF $M/out_$Name 2>&1 | grep -E "captures bit for bit")
  case "$Out" in *"0 beyond the tolerance"*"0 claims failed"*) if echo "$Out" | grep -q "^9 of 9"; then echo "MUTATION $Name: SURVIVED  $Out"; else echo "MUTATION $Name: caught (tolerance)  $Out"; fi;; *) echo "MUTATION $Name: caught    $Out";; esac
}
run RetentionWord   PlumeAdvection 's/Velocity = Wind + (Velocity - Wind) \* Plume.Retention.w;/Velocity = Wind + (Velocity - Wind) * Plume.Retention.z;/'
run RelaxDivisor    PlumePressure 's/- Divergence\[ResolveLocalIndex(Local)\] \* Edge \* Edge) \/ 6.0;/- Divergence[ResolveLocalIndex(Local)] * Edge * Edge) \/ 6.1;/'
run ConfineEdge     PlumeVorticity 's/Plume.Forces.z \* Plume.Lattice.y \* cross/Plume.Forces.z * Plume.Lattice.x * cross/'
run HeatRise        PlumeAdvection 's/SubgridHeatRise  = 1.4;/SubgridHeatRise  = 1.5;/'
run SunOctave       PlumeMarch 's/pow(Shadow.x, 0.25)/pow(Shadow.x, 0.26)/'
run SunTransmit     PlumeAdvection 's/ResolveOpticalDepth(Position, Plume.Sun.xyz) \* 0.65/ResolveOpticalDepth(Position, Plume.Sun.xyz) * 0.66/'
run CoverageGate    PlumeAdvection 's/Thermo.r > 0.002 || Thermo.a > 0.002/Thermo.r > 0.003 || Thermo.a > 0.002/'
run SourceFalloff   PlumeLayouts 's/exp(-1.7 \* Squared)/exp(-1.8 * Squared)/'
run BlackbodyBlue   PlumeLayouts 's/Hecto <= 19.0/Hecto <= 11.0/'
run FlameFlicker    PlumeMarch 's/0.45 + 1.1 \* lerp(Noise.a, Flicker, 0.25)/0.46 + 1.1 * lerp(Noise.a, Flicker, 0.25)/'
run BuoyancySat     PlumeAdvection 's/(1.0 + 0.125 \* Thermo.g)/(1.0 + 0.126 * Thermo.g)/'
run PushRate        PlumeAdvection 's/(4.0 + 0.15 \* (Source.Rates.x + Source.Rates.w)/(4.0 + 0.16 * (Source.Rates.x + Source.Rates.w)/'
run EdgeSolid       PlumeAdvection 's/ThermoSolidReach   = -0.25;/ThermoSolidReach   = -0.26;/'
run GroundBounce    PlumeMarch 's/\* 0.12 \* InversePi/* 0.13 * InversePi/'
run DetailDrift     PlumeMarch 's/-Plume.Wind.x \* 0.35/-Plume.Wind.x * 0.36/'
