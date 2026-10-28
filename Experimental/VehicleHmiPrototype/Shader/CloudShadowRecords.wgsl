struct CloudShadowParameters {
 Map:vec4f,      // world XY origin on Z=0, span, enabled
 Sun:vec4f,      // frozen sun direction used to generate the map, strength
 Quality:vec4f,  // softness (m), sample count, global base/top
}
