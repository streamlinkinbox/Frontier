// Z-up shared weather clock. Integrate velocity; editing direction/speed never rewrites past travel.
export class WindSequence{
 constructor(){this.Time=0;this.Phase=0;this.Offsets={Cloud:[0,0,0],LocalCloud:[0,0,0],LocalFog:[0,0,0]};this.Velocity=[0,0,0];}
 Advance(S,Delta){const DT=Math.max(0,Math.min(.1,Delta||0));this.Time+=DT;this.Phase+=DT*(.35+(S.WindGust??.25)*.4);
  const A=S.CloudWindBearing*Math.PI/180,G=1+(S.WindGust??.25)*(.65*Math.sin(this.Phase)+.35*Math.sin(this.Phase*2.17));
  const Speed=S.WindEnabled===false?0:S.CloudWindSpeed*G;this.Velocity=[Math.sin(A)*Speed,Math.cos(A)*Speed,0];
  for(const [Name,Altitude]of [['Cloud',S.CloudBase],['LocalCloud',S.LocalCloudZ],['LocalFog',S.LocalFogZ]]){
   if(Name==='LocalFog'&&S.LocalFogFollowWind===false){const Speed=S.LocalFogSpeed??.8;this.Offsets.LocalFog[0]+=Speed*DT;this.Offsets.LocalFog[1]+=Speed*DT*.57;this.Offsets.LocalFog[2]+=Speed*DT*.14;continue;}
   if(S[Name+'FollowWind']===false||S.WindEnabled===false)continue;
   const H=Math.max(0,Altitude||0)/1000,B=A+(S.WindVeer??8)*H*Math.PI/180,Shear=1+(S.WindShear??.15)*H;
   const Turb=(S.WindTurbulence??.15)*Speed;
   const V=[Math.sin(B)*Speed*Shear+Math.sin(this.Time*.31+H)*Turb,Math.cos(B)*Speed*Shear+Math.sin(this.Time*.27+H*3)*Turb,Math.sin(this.Time*.22+H)*Turb*.12];
   V.forEach((X,I)=>this.Offsets[Name][I]+=X*DT);
  }return this.Offsets;
 }
}
