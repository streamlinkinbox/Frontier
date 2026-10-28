// Transcribed AtmosphereModel.h::Twilight. Art-directed, explicitly not a physical scattering order.
fn NativeTwilight(D:vec3f,Sun:vec3f,Glow:f32)->vec3f {
 let Altitude=asin(clamp(D.z,-1.0,1.0))*57.2957795;let SunEl=asin(clamp(Sun.z,-1.0,1.0))*57.2957795;
 if(Altitude < -2.0){return vec3f(0.0);}
 let Window=smoothstep(-16.0,-5.0,SunEl)*(1.0-smoothstep(0.5,6.0,SunEl));if(Window<=0.0){return vec3f(0.0);}
 let Delta=atan2(sin(atan2(D.y,D.x)-atan2(Sun.y,Sun.x)),cos(atan2(D.y,D.x)-atan2(Sun.y,Sun.x)));
 let Az=exp(-pow(Delta/0.95,2.0));let AzWide=exp(-pow(Delta/1.8,2.0));let Alt=max(0.0,Altitude);let Depth=clamp(-SunEl/10.0,0.0,1.0);
 let C0=vec3f(1.0,0.88,0.62);let C1=vec3f(1.0,0.62,0.28);let C2=vec3f(0.95,0.42,0.30);let C3=vec3f(0.62,0.36,0.48);let C4=vec3f(0.25,0.30,0.58);
 let U=log2(1.0+Alt*2.0);var Colour=mix(C0,C1,smoothstep(0.0,1.6,U));Colour=mix(Colour,C2,smoothstep(1.6,2.9,U));Colour=mix(Colour,C3,smoothstep(2.9,4.0,U));Colour=mix(Colour,C4,smoothstep(4.0,5.2,U));Colour=mix(Colour,mix(C2,C4,0.6),Depth*0.6);
 let Env=exp(-Alt/mix(1.9,3.8,Depth))*(1.0-Depth*0.35);let Rim=exp(-Alt/0.45)*(1.0-Depth);
 let DomeWindow=smoothstep(-16.0,-8.0,SunEl)*(1.0-smoothstep(-2.0,4.0,SunEl));
 let Anti=exp(-pow((abs(Delta)-3.14159265)/.85,2.0));
 let Belt=vec3f(.32,.13,.23)*exp(-pow((Alt-4.0-Depth*3.0)/4.0,2.0))*Anti*Window*.025;
 return Belt*Glow+((Colour*Env*0.30+C0*Rim*0.25)*(Az*0.85+AzWide*0.15)*Window+vec3f(0.10,0.15,0.30)*0.035*DomeWindow*(1.0-exp(-Alt/6.0))*(1.0-0.5*Az))*Glow;
}
fn NativeTwilightLine(D:vec3f,Sun:vec3f,Glow:f32,Line:f32,Civil:f32)->vec3f {
 let Alt=asin(clamp(D.z,-1.0,1.0))*57.2957795;let El=asin(clamp(Sun.z,-1.0,1.0))*57.2957795;
 let Delta=atan2(sin(atan2(D.y,D.x)-atan2(Sun.y,Sun.x)),cos(atan2(D.y,D.x)-atan2(Sun.y,Sun.x)));
 let Window=select(1.0,smoothstep(-5.5,-2.5,El)*(1.0-smoothstep(-0.6,0.3,El)),Civil>0.5);
 return vec3f(1.0,0.98,0.92)*exp(-pow(Alt/0.11,2.0))*(0.7+0.3*smoothstep(-0.4,0.0,Alt))*exp(-pow(Delta/0.55,2.0))*Window*Line*0.45*Glow;
}
