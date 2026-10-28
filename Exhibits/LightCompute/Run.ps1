$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '../..')
function Confirm-Exit { if ($LASTEXITCODE -ne 0) { throw "Command failed: $LASTEXITCODE" } }
$Build = Join-Path (Get-Location) '_ClaudeScratch/build/light-compute-windows'
$Slang = if ($env:SLANGC) { $env:SLANGC } else { 'slangc' }
$Driver = if ($env:VULKAN_LIBRARY) { $env:VULKAN_LIBRARY } else { 'vulkan-1.dll' }
New-Item -ItemType Directory -Force $Build, '_ClaudeScratch/light-compute' | Out-Null
foreach ($Pair in @(@('AssignLightCells', 'Cells'), @('EvaluateLightReceivers', 'Receivers'), @('ReuseLightNeighbours', 'Reuse'), @('ReconstructLightHistory', 'Reconstruct'), @('FilterLightNeighbours', 'Filter'), @('FinalizeLightHistory', 'Finalize'), @('TracePrimarySurfaces', 'Primary'), @('TraceSurfaceTransport', 'Transport'), @('ReprojectSurfaceTransport', 'TransportTemporal'), @('FilterSurfaceTransport', 'TransportSpatial'), @('ResolveSurfaceTransport', 'TransportResolve'), @('BakeCloudRadiance', 'Clouds'), @('IntegrateFogScattering', 'Fog'))) {
    & $Slang 'Frontier/Engine/Rendering/Shaders/LightInfluence.slang' -entry $Pair[0] -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Build/$($Pair[1]).spv"
    Confirm-Exit
}
node Exhibits/VehicleReference/Code/Materials.mjs; Confirm-Exit
cmake -S Exhibits/VehicleReference -B _ClaudeScratch/build/vehicle-reference-windows; Confirm-Exit
cmake --build _ClaudeScratch/build/vehicle-reference-windows --config Release --target VehicleCapture; Confirm-Exit
& '_ClaudeScratch/build/vehicle-reference-windows/Release/VehicleCapture.exe' "$PWD/_ClaudeScratch/vehicle-reference" --vulkan-input; Confirm-Exit
& '_ClaudeScratch/build/vehicle-reference-windows/Release/VehicleCapture.exe' "$PWD/_ClaudeScratch/vehicle-reference" --motion-input; Confirm-Exit
cmake -S Exhibits/LightCompute -B $Build; Confirm-Exit
cmake --build $Build --config Release; Confirm-Exit
& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/vehicle-reference/VehicleLightInput.bin' '_ClaudeScratch/light-compute' '_ClaudeScratch/vehicle-reference'; Confirm-Exit
# Linux evidence JSON deliberately is not regenerated: it records the actually tested software device.

& '_ClaudeScratch/build/vehicle-reference-windows/Release/VehicleCapture.exe' "$PWD/_ClaudeScratch/vehicle-primary" --primary-input; Confirm-Exit
& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/vehicle-primary/VehiclePrimary.bin' '_ClaudeScratch/light-compute' '--primary-only'; Confirm-Exit

& '_ClaudeScratch/build/vehicle-reference-windows/Release/VehicleCapture.exe' "$PWD/_ClaudeScratch/primary-motion" --primary-motion; Confirm-Exit
& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--primary-motion' '_ClaudeScratch/primary-motion'; Confirm-Exit

& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/vehicle-primary/VehiclePrimary.bin' '_ClaudeScratch/light-compute' '--transport-only'; Confirm-Exit
& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--transport-motion' '_ClaudeScratch/primary-motion'; Confirm-Exit

& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/vehicle-primary/VehiclePrimary.bin' '_ClaudeScratch/light-compute' '--emissive-only'; Confirm-Exit
& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--emissive-motion' '_ClaudeScratch/primary-motion'; Confirm-Exit

& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--transport-reconstruction' '_ClaudeScratch/primary-motion'; Confirm-Exit

& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--clouds-only'; Confirm-Exit

& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--cloud-shadows'; Confirm-Exit

& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--fog-checks'; Confirm-Exit
& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--fog-rays'; Confirm-Exit

& "$Build/Release/LightComputeValidation.exe" $Driver $Build '_ClaudeScratch/primary-motion/VehicleMotion0.bin' '_ClaudeScratch/light-compute' '--fog-sky'; Confirm-Exit
