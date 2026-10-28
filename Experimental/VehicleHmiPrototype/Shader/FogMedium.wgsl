// Geometry fog shared by final-image recombination and the isolated GPU proof.
// Density [/m], distance/height [m]; squared optical depth matches native height fog.
fn FogTransmission(Camera:vec3f,Position:vec3f,Density:f32,Height:f32,AerialDensity:f32,AerialStart:f32,MieBlend:f32)->vec3f{
 let Distance=length(Position-Camera);let H=max(Height,0.1);
 let Delta=(Position.z-Camera.z)/H;var Column=1.0;
 if(abs(Delta)>0.001){Column=(1.0-exp(clamp(-Delta,-50.0,50.0)))/Delta;}
 let HeightOD=Density*Distance*exp(clamp(-Camera.z/H,-50.0,20.0))*Column;
 let AerialOD=AerialDensity*max(0.0,Distance-AerialStart);
 return exp(-min(vec3f(80.0),vec3f(HeightOD*HeightOD)+mix(vec3f(0.45,0.7,1.0),vec3f(1.0),MieBlend)*AerialOD));
}
