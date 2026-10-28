// Synthesized thunder, based on the preserved Particle Editor's brown-noise + crack design.
// No downloads, no audio context before an explicit gesture, bounded voices and cancellable future sounds.
export class ThunderAudio{
 constructor(){this.Enabled=false;this.Sources=new Set();this.Status='Sound off';}
 async Enable(){try{
  const AC=globalThis.AudioContext??globalThis.webkitAudioContext;if(!AC)throw Error('Web Audio unavailable');
  this.Context??=new AC();await this.Context.resume();if(this.Context.state!=='running')throw Error('Audio blocked by browser');
  if(!this.Rumble){this.Rumble=this.Noise(6,true);this.Crack=this.Noise(1,false);}
  this.Enabled=true;this.Status='Sound on';return true;
 }catch(E){this.Enabled=false;this.Status=String(E.message??E);return false;}}
 Noise(Seconds,Brown){const C=this.Context,B=C.createBuffer(1,Math.ceil(C.sampleRate*Seconds),C.sampleRate),D=B.getChannelData(0);let Last=0,Peak=.001;for(let I=0;I<D.length;I++){const W=Math.random()*2-1;Last=(Last+.02*W)/1.02;D[I]=Brown?Last*3.5:W;Peak=Math.max(Peak,Math.abs(D[I]));}for(let I=0;I<D.length;I++)D[I]*=.9/Peak;return B;}
 Stop(){for(const S of this.Sources){try{S.stop();}catch{}S.disconnect();}this.Sources.clear();}
 Disable(){this.Enabled=false;this.Stop();this.Status='Sound off';}
 Strike(Distance,Pan=0){
  const C=this.Context;if(!this.Enabled||C?.state!=='running')return false;
  const D=Math.max(1,Distance),T=C.currentTime+D/343,Level=Math.min(.6,240/Math.max(100,D));
  let Scheduled=false;const Voice=(Buffer,Type,Frequency,Gain,Duration,Attack)=>{
   if(this.Sources.size>=12)return;
   const S=C.createBufferSource(),F=C.createBiquadFilter(),G=C.createGain(),P=C.createStereoPanner();S.buffer=Buffer;F.type=Type;F.frequency.setValueAtTime(Frequency,T);if(Type==='lowpass')F.frequency.exponentialRampToValueAtTime(60,T+Duration);
   G.gain.setValueAtTime(.0001,T);G.gain.exponentialRampToValueAtTime(Math.max(.0002,Gain),T+Attack);G.gain.exponentialRampToValueAtTime(.0001,T+Duration);P.pan.value=Math.max(-1,Math.min(1,Pan));
   S.connect(F).connect(G).connect(P).connect(C.destination);this.Sources.add(S);S.onended=()=>{this.Sources.delete(S);S.disconnect();F.disconnect();G.disconnect();P.disconnect();};S.start(T,0,Duration+.05);Scheduled=true;
  };
  Voice(this.Rumble,'lowpass',Math.max(120,750-D*.12),Level,5,.12);
  if(D<1600)Voice(this.Crack,'highpass',1200,.45*(1-D/1600),.25,.004);
  return Scheduled;
 }
}
