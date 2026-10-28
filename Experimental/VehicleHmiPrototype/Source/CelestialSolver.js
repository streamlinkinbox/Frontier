// Low-precision orbital elements (Paul Schlyter formulation, J2000 epoch).
// Suitable for real-time visual placement, not navigation or scientific ephemerides.
const R=Math.PI/180, mod=(A,B=360)=>((A%B)+B)%B;
function Orbit(N,i,w,a,e,M) {
 M=mod(M)*R;let E=M;for(let J=0;J<6;J++)E-=(E-e*Math.sin(E)-M)/(1-e*Math.cos(E));
 const X=a*(Math.cos(E)-e),Y=a*Math.sqrt(1-e*e)*Math.sin(E),V=Math.atan2(Y,X),D=Math.hypot(X,Y);
 N*=R;i*=R;w*=R;
 return [D*(Math.cos(N)*Math.cos(V+w)-Math.sin(N)*Math.sin(V+w)*Math.cos(i)),D*(Math.sin(N)*Math.cos(V+w)+Math.cos(N)*Math.sin(V+w)*Math.cos(i)),D*Math.sin(V+w)*Math.sin(i)];
}
function Horizontal(P,JD,Lat,Lon) {
 const Ob=(23.4393-3.563e-7*(JD-2451543.5))*R;
 const X=P[0],Y=P[1]*Math.cos(Ob)-P[2]*Math.sin(Ob),Z=P[1]*Math.sin(Ob)+P[2]*Math.cos(Ob);
 const RA=Math.atan2(Y,X),Dec=Math.atan2(Z,Math.hypot(X,Y));
 const LST=mod(280.46061837+360.98564736629*(JD-2451545)+Lon)*R;
 const H=LST-RA,L=Lat*R;
 const East=-Math.cos(Dec)*Math.sin(H),North=Math.sin(Dec)*Math.cos(L)-Math.cos(Dec)*Math.cos(H)*Math.sin(L),Up=Math.sin(Dec)*Math.sin(L)+Math.cos(Dec)*Math.cos(H)*Math.cos(L);
 // Engine azimuth is counterclockwise from +X east, Z up.
 return {Direction:[East,North,Up],Azimuth:Math.atan2(North,East)/R,Elevation:Math.asin(Math.max(-1,Math.min(1,Up)))/R};
}
export function SolveCelestial(S) {
 const DateText=S.SkyDate||'2026-10-20';
 const DateMs=Date.parse(DateText+'T00:00:00Z');
 const JD=(Number.isFinite(DateMs)?DateMs:Date.UTC(2026,9,20))/86400000+2440587.5+((S.SunTime??12)-(S.TimeZone??2))/24;
 const D=JD-2451543.5;
 const Sun=Orbit(0,0,282.9404+4.70935e-5*D,1,0.016709-1.151e-9*D,356.047+0.9856002585*D);
 const Moon=Orbit(125.1228-0.0529538083*D,5.1454,318.0634+0.1643573223*D,60.2666,.0549,115.3654+13.0649929509*D);
 const Lat=S.Latitude??-26.18,Lon=S.Longitude??28.32;
 const Bodies=[
 ['Venus',76.6799+2.4659e-5*D,3.3946+2.75e-8*D,54.891+1.38374e-5*D,.72333,.006773-1.302e-9*D,48.0052+1.6021302244*D],
 ['Jupiter',100.4542+2.76854e-5*D,1.303-1.557e-7*D,273.8777+1.64505e-5*D,5.20256,.048498+4.469e-9*D,19.895+.0830853001*D],
 ['Mars',49.5574+2.11081e-5*D,1.8497-1.78e-8*D,286.5016+2.92961e-5*D,1.523688,.093405+2.516e-9*D,18.6021+.5240207766*D],
 ['Saturn',113.6634+2.3898e-5*D,2.4886-1.081e-7*D,339.3939+2.97661e-5*D,9.55475,.055546-9.499e-9*D,316.967+.0334442282*D]
 ];
 const Planets=Bodies.map(([Name,...E])=>{const H=Orbit(...E);return {Name,...Horizontal(H.map((V,I)=>V+Sun[I]),JD,Lat,Lon)};});
 return {Sun:Horizontal(Sun,JD,Lat,Lon),Moon:{...Horizontal(Moon,JD,Lat,Lon),Phase:mod(JD-2451550.1,29.530588853)},Planets,JD};
}
export function AdvanceCelestial(S,Delta) {
 if(S.DayAnimate){S.SunTime=mod(S.SunTime+Delta*24/(Math.max(.01,S.DayDuration)*3600),24);S.SunAutoPosition=true;}
 const Solved=SolveCelestial(S);
 if(S.SunAutoPosition){S.SunAzimuth=Solved.Sun.Azimuth;S.SunElevation=Solved.Sun.Elevation;}
 for(const Moon of [S,...(S.MoonInstances??[])]) if(Moon.MoonFollowSky){Moon.MoonAzimuth=Solved.Moon.Azimuth;Moon.MoonElevation=Solved.Moon.Elevation;Moon.MoonPhase=Solved.Moon.Phase;}
 return Solved;
}
