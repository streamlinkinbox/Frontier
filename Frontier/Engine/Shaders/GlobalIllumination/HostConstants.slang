//============================================================================================================================================
//                                                          HOSTCONSTANTS.SLANG
//============================================================================================================================================
// 🧩 Host-resolved scene constants (ground lattice, card anchors, material families) as Vulkan specialization constants.

#pragma once

// 📝 The demo prepends these as WGSL 'const' lines to every program it builds after the card depot exists; here they are
//    specialization constants with the same names, resolved by the host at pipeline creation (IlluminationConstants).
//    ⚠️ Identifier order is the constant_id — it must match IlluminationConstants::SpecializationOrder in C++.

[[vk::constant_id(0)]] const uint MaximumMarchSteps = 96;
[[vk::constant_id(1)]] const float GroundPadMinimumX = -60.0;
[[vk::constant_id(2)]] const float GroundPadMinimumY = -60.0;
[[vk::constant_id(3)]] const float GroundTileExtent = 12.0;
[[vk::constant_id(4)]] const uint GroundTileCountX = 18;
[[vk::constant_id(5)]] const uint GroundTileCountY = 10;
[[vk::constant_id(6)]] const uint GroundFirstCard = 0;
[[vk::constant_id(7)]] const uint GroundStripCard = 0;
[[vk::constant_id(8)]] const uint GroundOccupantIndex = 0;
[[vk::constant_id(9)]] const uint SurroundFamily = 0;
[[vk::constant_id(10)]] const float TyreMarkOriginX = 0.0;
[[vk::constant_id(11)]] const float TyreMarkOriginY = 0.0;
[[vk::constant_id(12)]] const float TyreMarkExtentX = 1.0;
[[vk::constant_id(13)]] const float TyreMarkExtentY = 1.0;
[[vk::constant_id(14)]] const float TyreMarkPatchWidth = 1.0;
[[vk::constant_id(15)]] const uint RimMarkFamily = 0;
[[vk::constant_id(16)]] const uint TyreMarkFamily = 0;
[[vk::constant_id(17)]] const uint MarkedFamilyFirst = 0;
[[vk::constant_id(18)]] const uint MarkedFamilyLast = 0;
[[vk::constant_id(19)]] const uint MarkedFamilyExtraFirst = 0;
[[vk::constant_id(20)]] const uint MarkedFamilyExtraLast = 0;
[[vk::constant_id(21)]] const uint DerezGlowFamily = 0;
[[vk::constant_id(22)]] const uint DerezHotFamily = 0;
[[vk::constant_id(23)]] const uint DerezGlitchFamily = 0;
[[vk::constant_id(24)]] const uint VehicleGlassFamily = 0;
[[vk::constant_id(25)]] const uint SteeringWheelFamily = 0;
[[vk::constant_id(26)]] const uint TabletHousingFamily = 0;
[[vk::constant_id(27)]] const uint DisplayGlassFamily = 0;
[[vk::constant_id(28)]] const uint TyreFamily = 0;
[[vk::constant_id(29)]] const uint CockpitFamilyFirst = 0;
[[vk::constant_id(30)]] const uint CockpitFamilyLast = 0;
[[vk::constant_id(31)]] const uint SeatFabricFamily = 0;
[[vk::constant_id(32)]] const uint MirrorGlassFamily = 0;
[[vk::constant_id(33)]] const uint ConeOrangeFamily = 0;
[[vk::constant_id(34)]] const uint ConeBandFamily = 0;
[[vk::constant_id(35)]] const uint ConeBaseFamily = 0;
