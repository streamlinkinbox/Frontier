/*==========================================================================================================================================
                                                            PHYSICSINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 PhysicsConfiguration: mass, kinematics and contact response.

#include "Engine/Interface/Inspectors/PhysicsInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

void PhysicsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Dynamic Body", "Rigid solver", &Parameters.DynamicBody});
    OutSwitches.push_back({"Gravity", "-9.81 m/s2", &Parameters.GravityCoupled});
}

void PhysicsInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    InspectorControls::ConstructSectionDivider(Palette, Faces, "MASS CONFIGURATION", "m = rho * V");
    bool Changed = false;
    if (InspectorControls::ConstructSliderRow(Palette, Faces, "##MassKg", "Mass", Parameters.Mass.MassKilograms, 0.01f, 5000.0f, "%.2f kg"))
    {
        Parameters.Mass.SyncVolumeFromMassAndDensity();
        Changed = true;
    }
    if (InspectorControls::ConstructSliderRow(Palette, Faces, "##Density", "Density", Parameters.Mass.DensityKgPerCubicMetre, 1.0f, 20000.0f, "%.0f kg/m3"))
    {
        Parameters.Mass.SyncMassFromDensityAndVolume();
        Changed = true;
    }
    if (InspectorControls::ConstructSliderRow(Palette, Faces, "##Volume", "Volume", Parameters.Mass.VolumeCubicMetres, 0.0001f, 25.0f, "%.4f m3"))
    {
        Parameters.Mass.SyncMassFromDensityAndVolume();
        Changed = true;
    }

    InspectorControls::ConstructSectionDivider(Palette, Faces, "KINEMATICS & CONTACT");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##VelX", "Linear Vel X", Parameters.LinearVelocityMps[0], -80.0f, 80.0f, "%.2f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##VelY", "Linear Vel Y", Parameters.LinearVelocityMps[1], -80.0f, 80.0f, "%.2f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##VelZ", "Linear Vel Z", Parameters.LinearVelocityMps[2], -80.0f, 80.0f, "%.2f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Restit", "Restitution", Parameters.Restitution, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##StatFric", "Static Friction", Parameters.StaticFriction, 0.0f, 1.5f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##DynFric", "Dynamic Friction", Parameters.DynamicFriction, 0.0f, 1.5f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##LinDamp", "Linear Damping", Parameters.LinearDamping, 0.0f, 2.0f, "%.2f");

    const float Speed    = Parameters.ComputeLinearSpeedMps();
    const float KineticJ = Parameters.Mass.ComputeKineticEnergyJoules(Speed);
    const float Momentum = Parameters.Mass.ComputeLinearMomentumNs(Speed);
    char        KineticBuf[48]{};
    char        MomentumBuf[48]{};
    std::snprintf(KineticBuf, sizeof(KineticBuf), "%.2f J", KineticJ);
    std::snprintf(MomentumBuf, sizeof(MomentumBuf), "%.2f N*s", Momentum);
    const std::array<InspectorReadoutCell, 2> Cells = {{
        {"KINETIC ENERGY", KineticBuf},
        {"MOMENTUM", MomentumBuf},
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());

    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void PhysicsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void PhysicsInspector::ConstructInspector() {}

#endif

} // namespace Frontier
