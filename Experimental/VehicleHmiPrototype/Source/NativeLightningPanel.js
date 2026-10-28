import {Card,Tiles,Note,Slider,Select,Button} from './NativeEnvironmentControls.js';
export function BuildLightningControls(H,C,P){
 const S=H.Settings,L=H.Lightning,Box=Card(P,'Lightning & thunder','Sparkles');
 Note(Box,'Opt-in weather effect · bright flashes. Cloud-spanning illumination and branching particle channels; exposure and sun settings are unchanged.');
 Tiles(C,Box,S,[['LightningEnabled','Lightning','Power'],['LightningAuto','Automatic strikes','Power']]);
 Select(C,Box,S,'LightningMode','Discharge',[['0','Cloud → ground'],['1','Inside cloud']]);
 Select(C,Box,S,'LightningCloudTarget','Cloud source',[['0','Auto · global, or local if no global'],['1','Global cloud layer'],['2','Local cloud volume']]);
 Button(Box,'Strike ahead',()=>{L.Trigger();C.Notify('LightningEnabled',S);C.Synchronise();});
 const Sound=Button(Box,'Enable thunder sound',async()=>{if(L.Audio.Enabled)L.Audio.Disable();else await L.Audio.Enable();Sync();});
 const Sync=()=>{const Text=L.Audio.Enabled?'Mute thunder':L.Audio.Status==='Sound off'?'Enable thunder sound':L.Audio.Status;if(Sound.textContent!==Text)Sound.textContent=Text;};C.Live.push(Sync);
 Button(Box,'Author lightning in Particle Editor',()=>H.ParticleWorkspace.OpenLightning());
 Slider(C,Box,S,'LightningDistance','Approx. strike distance',50,6000,50,'m');
 Slider(C,Box,S,'LightningIntervalMin','Minimum interval',2,60,.5,'s',()=>{S.LightningIntervalMax=Math.max(S.LightningIntervalMin,S.LightningIntervalMax);});Slider(C,Box,S,'LightningIntervalMax','Maximum interval',2,60,.5,'s',()=>{S.LightningIntervalMin=Math.min(S.LightningIntervalMin,S.LightningIntervalMax);});
 Slider(C,Box,S,'LightningSpread','Horizontal cloud channel span',400,10000,100,'m');
 Slider(C,Box,S,'LightningBrightness','Bolt brightness',0,3,.05);Slider(C,Box,S,'LightningCloudGlow','Cloud flash strength',0,3,.05);Slider(C,Box,S,'LightningRadius','Cloud flash radius',200,6000,100,'m');
 const Status=Note(Box,'');const Read=()=>{const B=L.Storm.Bolts[0];const Text=`${L.Storm.Strikes} strikes · ${B?.Segments.length??0} active ribbon segments${B?.Anchor?' · '+B.Anchor.Medium+' cloud: '+(B.Anchor.Embedded?'source embedded':'no dense source found'):''} · thunder ≈ ${(S.LightningDistance/343).toFixed(1)} s at ground-level strike distance${H.Metrics?.Enabled?' · bolt GPU '+(H.Metrics.Durations.Lightning??0).toFixed(3)+' ms (last sample)':''}`;if(Status.textContent!==Text)Status.textContent=Text;};C.Live.push(Read);Read();
 Note(Box,'Sound requires a click; distance determines delay, filtering and stereo position. In-cloud thunder uses 3D distance. No strike-impact sparks or guaranteed terrain attachment yet; ground endpoints use Z=0. Cloud scattering is a bounded approximation, not full multiple-scattering transport.');
}
