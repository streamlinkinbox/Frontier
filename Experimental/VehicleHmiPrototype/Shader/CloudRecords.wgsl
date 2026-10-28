// Shared cloud / bounded-fog ABI, 544 bytes. First eight vectors preserve display/shadow controls.
struct FogLamp {PositionType:vec4f,DirectionReach:vec4f,ColourIntensity:vec4f,Params:vec4f}
struct CloudParameters {Layer:vec4f,Shape:vec4f,Centre:vec4f,Size:vec4f,Local:vec4f,Control:vec4f,Wind:vec4f,Render:vec4f,
 Formation:vec4f,LocalShape:vec4f,Advection:vec4f,FogCentre:vec4f,FogSize:vec4f,FogMedium:vec4f,FogMaterial:vec4f,FogLighting:vec4f,FogWind:vec4f,FogControl:vec4f,Lamps:array<FogLamp,4>}
