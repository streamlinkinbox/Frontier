#!/bin/bash
#============================================================================================================================================
#                                                        MUTATEGASINSTRUMENTS.SH
#============================================================================================================================================
# 🧩 Mutation check of the two editor instruments (the decay curves and the blackbody strip): each mutant changes one constant or one line of
#    GasInstrumentProjection.cpp, rebuilds PlumeSimulationValidation and expects it to fail against Results/PlumeReference.toml, whose [Instruments]
#    section the prototype's own DrawDecay and DrawBlackbody wrote (Code/ExportPlumeReference.mjs). The original source is restored after the last
#    mutant, and on exit.
#
#        Exhibits/PlumeSimulation/Code/MutateGasInstruments.sh <build folder> > Exhibits/PlumeSimulation/Results/GasInstrumentMutations.txt
#
#    It edits the repository's source: if it is stopped before its trap runs, restore it with
#        git checkout -- Frontier/Engine/PhysicalDynamics/FluidDynamics/GasInstrumentProjection.cpp
#    MUTATE_DRY_RUN=1 only checks that every pattern still matches the source (a mutant that changes nothing is reported as NO CHANGE).
set -u
R=$(cd "$(dirname "$0")/../../.." && pwd)
Build=$(cd "${1:?usage: MutateGasInstruments.sh <build folder>}" && pwd)
F=$R/Frontier/Engine/PhysicalDynamics/FluidDynamics/GasInstrumentProjection.cpp
Scratch=$(mktemp -d)
Original=$Scratch/GasInstrumentProjection.cpp.orig
DryRun=${MUTATE_DRY_RUN:-0}
export PATH=$HOME/.local/bin:$PATH
cp "$F" "$Original"
Caught=0
Total=0
restore()
{
  cp "$Original" "$F"
  if [ "$DryRun" != 1 ]; then (cd "$Build" && ninja -j2 PlumeSimulationValidation > /dev/null 2>&1); fi
  rm -rf "$Scratch"
}
trap restore EXIT
run() # name, sed expression, [why a survivor is expected]
{
  cp "$Original" "$F"
  sed -i "$2" "$F"
  if cmp -s "$F" "$Original"; then echo "MUTANT $1: NO CHANGE (pattern missing)"; return; fi
  if [ "$DryRun" = 1 ]; then echo "MUTANT $1: pattern matches"; return; fi
  if ! (cd "$Build" && ninja -j2 PlumeSimulationValidation > "$Scratch/build.log" 2>&1); then echo "MUTANT $1: BUILD FAILED"; return; fi
  Total=$((Total + 1))
  local Out
  Out=$(cd "$R" && "$Build/PlumeSimulationValidation" . 2>&1 | tail -1)
  if echo "$Out" | grep -q " 0 failures"; then echo "MUTANT $1: SURVIVED ($Out)${3:+ — $3}"; else Caught=$((Caught + 1)); echo "MUTANT $1: caught ($Out)"; fi
}

# 📝 ResolveHellandColour: the clamp, the six fit constants (each changed by 1e-10 and by 1e-6), the blue gate, the ceilings, the gamma.
run HellandLow        's|std::max(1000.0, std::min(6600.0, Kelvin))|std::max(900.0, std::min(6600.0, Kelvin))|'
run HellandHigh       's|std::min(6600.0, Kelvin)|std::min(6700.0, Kelvin)|'
run GreenSlope        's|99.4708025861|99.4708025862|'
run GreenSlope6       's|99.4708025861|99.4708035861|'
run GreenOffset       's|161.1195681661|161.1195681662|'
run GreenOffset6      's|161.1195681661|161.1195691661|'
run BlueSlope         's|138.5177312231|138.5177312232|'
run BlueSlope6        's|138.5177312231|138.5177322231|'
run BlueOffset        's|305.0447927307|305.0447927308|'
run BlueOffset6       's|305.0447927307|305.0447937307|'
run BlueShift         's|std::max(Hecto - 10.0, 1.0)|std::max(Hecto - 11.0, 1.0)|'
run BlueGateLow       's|Hecto <= 19.0|Hecto <= 18.0|' "equivalent: between Hecto 18 and 19 the fit's blue is negative and clamps to 0, as the gate does"
run BlueGateHigh      's|Hecto <= 19.0|Hecto <= 25.0|'
run GreenCeiling      's|Green = std::min(1.0,|Green = std::min(0.9,|'
run BlueCeiling       's|: std::min(1.0, std::max(0.0, (138|: std::min(0.9, std::max(0.0, (138|'
run RedChannel        's|return { 1.0, std::pow(Green|return { 0.9, std::pow(Green|'
run GreenGamma        's|std::pow(Green, 2.2)|std::pow(Green, 2.1)|'
run BlueGamma         's|std::pow(Blue, 2.2)|std::pow(Blue, 2.3)|'

# 📝 ConstructDecayInstrument: the margins, the grid, the two curves and the labels.
run GridRows          's|for (int Line = 0; Line <= 4; ++Line)|for (int Line = 0; Line <= 3; ++Line)|'
run GridSpacing       's|(Bottom - Top) \* Line / 4;|(Bottom - Top) * Line / 5;|'
run GridColour        's|{ 0x2c, 0x2c, 0x2c }|{ 0x2c, 0x2c, 0x2d }|'
run GridWidth         's|{ GridInk, 1.0, {|{ GridInk, 1.5, {|'
run DecayMarginTop    's|Top = 18.0, Bottom|Top = 19.0, Bottom|'
run DecayMarginBottom 's|Bottom = Height - 26.0|Bottom = Height - 27.0|'
run DecayMarginRight  's|Right = Width - 18.0, Top|Right = Width - 19.0, Top|'
run CurveWidth        's|InstrumentPolyline Stroke{ Colour, 3.0, {} }|InstrumentPolyline Stroke{ Colour, 2.0, {} }|'
run CurveSteps        's|Step <= 60; ++Step|Step <= 59; ++Step|'
run CurveSpan         's|static_cast<double>(Step) / 60 \* 12.0|static_cast<double>(Step) / 60 * 11.0|'
run CurveSurvival     's|std::max(0.0, 1.0 - Loss)|std::max(0.0, 1.0 - Loss * 0.99)|'
run CurveX            's|(Right - Left) \* Step / 60;|(Right - Left) * Step / 61;|'
run CurveDensity      's|Curve(Gas.Dissipation, DensityInk)|Curve(Gas.Cooling, DensityInk)|'
run CurveHeatInk      's|Curve(Gas.Cooling, HeatInk)|Curve(Gas.Cooling, DensityInk)|'
run DensityLabel      's|"density", Left, Height - 4|"density", Left, Height - 5|'
run HeatLabelX        's|Left + 80, Height - 4|Left + 81, Height - 4|'
run EndLabelX         's|"12 s", Right - 40|"12 s", Right - 41|'
run LabelColour       's|{ 0x7d, 0x7d, 0x7d }|{ 0x7d, 0x7d, 0x7e }|'

# 📝 ConstructBlackbodyInstrument: the bands, the temperature ramp, the brightness, the tone map and the labels.
run BandStep          's|X += 2)|X += 3)|'
run KelvinRamp        's|Tip + (Core - Tip) \* Mapped;|Tip + (Core - Tip) * Mapped * 0.99;|'
run BrightnessPower   's|std::pow(Kelvin / 1500.0, 4.0)|std::pow(Kelvin / 1500.0, 4.1)|'
run BrightnessScale   's|Kelvin / 1500.0|Kelvin / 1501.0|'
run BrightnessCeiling 's|std::min(1.6, std::pow|std::min(1.5, std::pow|'
run ToneDenominator   's|/ (1.0 + Colour\[Channel\] \* Brightness)|/ (1.1 + Colour[Channel] * Brightness)|'
run ToneGain          's|std::min(1.0, Lit) \* 1.6, 1 / 2.2|std::min(1.0, Lit) * 1.5, 1 / 2.2|'
run ToneGamma         's|1 / 2.2) + 0.5);|1 / 2.1) + 0.5);|'
run ToneRounding      's|1 / 2.2) + 0.5);   // Math.round|1 / 2.2));   // Math.round|'
run BandTop           's|{ X, 14.0, 2.0, Height - 50, Tone }|{ X, 15.0, 2.0, Height - 50, Tone }|'
run BandWidth         's|{ X, 14.0, 2.0, Height - 50, Tone }|{ X, 14.0, 3.0, Height - 50, Tone }|'
run BandHeight        's|{ X, 14.0, 2.0, Height - 50, Tone }|{ X, 14.0, 2.0, Height - 51, Tone }|'
run TipTemperature    's|Gas.FlameTip \* Gas.FlameTemperature, Core|Gas.FlameTip * Gas.FlameTemperature * 1.001, Core|'
run CoreTemperature   's|Gas.FlameCore \* Gas.FlameTemperature;|Gas.FlameCore * Gas.FlameTemperature * 1.001;|'
run TipLabelY         's|" K", Left, Height - 8|" K", Left, Height - 9|'
run CoreLabelX        's|Right - 100, Height - 8|Right - 101, Height - 8|'
run TipWording        's|"tip " + FormatWhole(Tip)|"tip  " + FormatWhole(Tip)|'
run CoreWording       's|" K", Right - 100|"K", Right - 100|'
run LabelRounding     's|std::floor(Value + 0.5)|std::floor(Value)|'

if [ "$DryRun" != 1 ]; then echo "$Caught of $Total mutants caught"; fi
