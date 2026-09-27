// ═══════════════════════════════════════════════════════════════════
// ui.js — cinematic camera, HUD, anatomy labels, wing-whine audio
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';

const clamp=THREE.MathUtils.clamp, lerp=THREE.MathUtils.lerp;
const damp=(a,b,l,dt)=>lerp(a,b,1-Math.exp(-l*dt));

// ── orbit / track / macro / cine camera ────────────────────────────
export class Camera{
  constructor(canvas, world){
    this.canvas=canvas; this.world=world;
    this.cam=new THREE.PerspectiveCamera(42, 1, 0.5, 1200);
    this.mode='track';
    this.target=new THREE.Vector3(0,6,0); this.goalT=this.target.clone();
    this.yaw=0.7; this.pitch=0.42; this.dist=30;
    this.gYaw=this.yaw; this.gPitch=this.pitch; this.gDist=this.dist;
    this.cineA=0;
    this._bind();
    this.resize();
  }
  resize(){
    const w=this.canvas.clientWidth||innerWidth, h=this.canvas.clientHeight||innerHeight;
    this.cam.aspect=w/h; this.cam.updateProjectionMatrix();
  }
  _bind(){
    const c=this.canvas;
    let drag=null, pinch=0;
    c.addEventListener('contextmenu',e=>e.preventDefault());
    c.addEventListener('pointerdown',e=>{
      c.setPointerCapture(e.pointerId);
      drag={x:e.clientX,y:e.clientY,b:e.button};
    });
    c.addEventListener('pointermove',e=>{
      if(!drag) return;
      const dx=e.clientX-drag.x, dy=e.clientY-drag.y;
      drag.x=e.clientX; drag.y=e.clientY;
      if(drag.b===2){ // pan
        const s=this.dist*0.0016;
        const fwd=new THREE.Vector3(); this.cam.getWorldDirection(fwd);
        const right=new THREE.Vector3().crossVectors(fwd,this.cam.up).normalize();
        const up=new THREE.Vector3().crossVectors(right,fwd).normalize();
        this.goalT.addScaledVector(right,-dx*s).addScaledVector(up,dy*s);
        if(this.mode!=='free') this.setMode('free',true);
      } else { // orbit
        this.gYaw-=dx*0.0052; this.gPitch=clamp(this.gPitch+dy*0.0052,-1.2,1.45);
      }
    });
    const up=e=>{ drag=null; };
    c.addEventListener('pointerup',up); c.addEventListener('pointercancel',up);
    c.addEventListener('wheel',e=>{
      e.preventDefault();
      this.gDist=clamp(this.gDist*(1+Math.sign(e.deltaY)*0.1), 4, 320);
    },{passive:false});
    c.addEventListener('touchmove',e=>{
      if(e.touches.length===2){
        e.preventDefault();
        const d=Math.hypot(e.touches[0].clientX-e.touches[1].clientX,
                            e.touches[0].clientY-e.touches[1].clientY);
        if(pinch) this.gDist=clamp(this.gDist*(pinch/d),4,320);
        pinch=d;
      }
    },{passive:false});
    c.addEventListener('touchend',()=>pinch=0);
  }
  setMode(m, silent){
    this.mode=m;
    if(m==='macro') this.gDist=10;
    if(m==='track'&&this.gDist<12) this.gDist=34;
    if(!silent && this.onMode) this.onMode(m);
  }
  update(dt, focus, headPos){
    if(this.mode==='track'||this.mode==='cine') this.goalT.lerp(focus,1-Math.exp(-dt*5));
    if(this.mode==='macro') this.goalT.lerp(headPos,1-Math.exp(-dt*6));
    if(this.mode==='cine'){ this.cineA+=dt*0.12; this.gYaw=this.cineA; this.gPitch=0.32; }
    this.yaw=damp(this.yaw,this.gYaw,10,dt);
    this.pitch=damp(this.pitch,this.gPitch,10,dt);
    this.dist=damp(this.dist,this.gDist,8,dt);
    this.target.lerp(this.goalT,1-Math.exp(-dt*10));
    const cp=Math.cos(this.pitch), sp=Math.sin(this.pitch);
    _p.set(this.target.x+Math.sin(this.yaw)*cp*this.dist,
           this.target.y+sp*this.dist,
           this.target.z+Math.cos(this.yaw)*cp*this.dist);
    // keep out of the ground
    const gh=this.world.groundH(_p.x,_p.z)+1.2;
    if(_p.y<gh)_p.y=gh;
    this.cam.position.copy(_p);
    this.cam.lookAt(this.target);
  }
}
const _p=new THREE.Vector3();

// ── HUD ─────────────────────────────────────────────────────────────
const $=id=>document.getElementById(id);
export class HUD{
  constructor(){
    this.labelsOn=true; this.trailsOn=false;
    this.logEl=$('log');
    // anatomy label defs
    this.labelDefs=[
      ['eye','compound eye'],['antenna','pilose antenna ♀'],['palp','maxillary palp'],
      ['fascicle','fascicle · 6 stylets','amber'],['labium','labium sheath','amber'],
      ['wing','scaled wing'],['haltere','haltere'],['scutum','scutum · lyre'],
      ['knee','femoro-tibial joint'],['tarsus','tarsus · 5 tarsomeres'],
      ['pulvillus','pulvillus + claws'],['abdomen','abdomen · midgut'],
      ['cercus','cercus ♀'],
    ];
    this.labelEls={};
    const wrap=$('labels');
    for(const [key,text,cls] of this.labelDefs){
      const d=document.createElement('div');
      d.className='alabel'+(cls?' '+cls:''); d.textContent=text;
      wrap.appendChild(d); this.labelEls[key]=d;
    }
    this.cb={};
    this._wire();
    // wing-whine synth (true pitch ≈ 560 Hz, whisper-quiet)
    this.audioOn=false; this._ac=null;
  }
  _wire(){
    const seg=$('camseg');
    seg.querySelectorAll('button').forEach(b=>b.addEventListener('click',()=>{
      seg.querySelectorAll('button').forEach(o=>o.classList.remove('on'));
      b.classList.add('on'); this.cb.cam?.(b.dataset.cam);
    }));
    this.setCamMode=m=>{
      seg.querySelectorAll('button').forEach(o=>o.classList.toggle('on',o.dataset.cam===m));
    };
    const btn=(id,fn)=>$(id).addEventListener('click',fn);
    btn('btn-takeoff',()=>this.cb.takeoff?.());
    btn('btn-land',()=>this.cb.land?.());
    btn('btn-walk-deck',()=>this.cb.walkDeck?.());
    btn('btn-walk-rock',()=>this.cb.walkRock?.());
    btn('btn-climb',()=>this.cb.climb?.());
    btn('btn-ceiling',()=>this.cb.ceiling?.());
    btn('btn-feed-fuel',()=>this.cb.feedFuel?.());
    btn('btn-feed-blood',()=>this.cb.feedBlood?.());
    btn('btn-withdraw',()=>this.cb.withdraw?.());
    btn('btn-escape',()=>this.cb.escape?.());
    btn('btn-reset',()=>this.cb.reset?.());
    btn('btn-guide',()=>$('guide').classList.remove('hidden'));
    btn('btn-guide-close',()=>$('guide').classList.add('hidden'));
    $('guide').addEventListener('click',e=>{ if(e.target.id==='guide')e.target.classList.add('hidden'); });
    const tg=(id,fn)=>$(id).addEventListener('click',()=>{ $(id).classList.toggle('on'); fn($(id).classList.contains('on')); });
    tg('tg-labels',on=>{ this.labelsOn=on; });
    tg('tg-trail',on=>{ this.trailsOn=on; this.cb.trails?.(on); });
    tg('tg-audio',on=>this.audio(on));
    tg('tg-demo',on=>this.cb.demo?.(on));
    $('sl-speed').addEventListener('input',e=>{
      const v=e.target.value/100, s=(2+v*20).toFixed(0);
      $('v-speed').textContent=`${s} mm/s`; this.cb.speed?.(2+v*20);
    });
    $('sl-time').addEventListener('input',e=>{
      const v=e.target.value/100, ts=Math.pow(10,-2.3+2.3*v); // 0.005×…1×
      $('v-time').textContent=`${ts<0.1?ts.toFixed(3):ts.toFixed(2)}×`;
      this.cb.time?.(ts);
    });
    $('sl-blur').addEventListener('input',e=>{
      $('v-blur').textContent=`${e.target.value}%`; this.cb.blur?.(e.target.value/100);
    });
  }
  on(evt,fn){ this.cb[evt]=fn; }
  setState(s, cls=''){
    const b=$('state-badge'); b.textContent=s;
    b.className='state-badge '+cls;
  }
  log(msg){
    const t=new Date(), p=n=>String(n).padStart(2,'0');
    this.logEl.innerHTML=`<span class="t">${p(t.getMinutes())}:${p(t.getSeconds())}</span>${msg}`;
  }
  chips(wbf, stroke, gait, fps){
    $('chip-wbf').textContent=`${wbf.toFixed(0)} Hz`;
    $('chip-stroke').textContent=`${stroke.toFixed(0)}°`;
    $('chip-gait').textContent=gait;
    $('chip-fps').textContent=fps.toFixed(0);
  }
  gauges(fill, fluid, tank, dish){
    $('g-fill').style.width=`${fill*100}%`;
    $('g-fill').className='gauge-fill '+(fluid==='fuel'?'fuel':'blood');
    $('v-fill').textContent=`${(fill*100).toFixed(0)}%`;
    $('v-fluid').textContent=fill<0.02?'unfed':`${fluid} · ${fill<0.6?'feeding':fill<0.95?'engorging':'REPLETE'}`;
    $('g-tank').style.width=`${tank*100}%`; $('v-tank').textContent=`${(tank*100).toFixed(0)}%`;
    $('g-dish').style.width=`${dish*100}%`; $('v-dish').textContent=`${(dish*100).toFixed(0)}%`;
  }
  tele(o){
    $('t-speed').textContent=`${o.speed.toFixed(0)} mm/s`;
    $('t-alt').textContent=`${o.alt.toFixed(1)} mm`;
    $('t-surf').textContent=o.surf;
    $('t-grip').textContent=o.grip;
    $('t-duty').textContent=o.duty;
    $('t-phase').textContent=o.phase;
  }
  feedSteps(active){
    document.querySelectorAll('#feed-steps li').forEach(li=>{
      const order=['seek','probe','insert','salivate','engorge','replete'];
      const i=order.indexOf(li.dataset.s), a=order.indexOf(active);
      li.className=a<0?'':(i<a?'done':(i===a?'active':''));
    });
  }
  projectLabels(cam, anchors){
    for(const [key] of this.labelDefs){
      const el=this.labelEls[key], p=anchors[key];
      if(!p||!this.labelsOn){ el.classList.remove('show'); continue; }
      _v.copy(p).project(cam);
      const behind=_v.z>1;
      if(behind){ el.classList.remove('show'); continue; }
      el.style.left=`${(_v.x*0.5+0.5)*innerWidth+14}px`;
      el.style.top=`${(-_v.y*0.5+0.5)*innerHeight-10}px`;
      el.classList.add('show');
    }
  }
  audio(on){
    this.audioOn=on;
    if(on&&!this._ac){
      const AC=window.AudioContext||window.webkitAudioContext;
      if(!AC) return;
      const ac=this._ac=new AC();
      this._osc=ac.createOscillator(); this._osc.type='sawtooth';
      this._osc2=ac.createOscillator(); this._osc2.type='sine';
      this._lp=ac.createBiquadFilter(); this._lp.type='lowpass'; this._lp.frequency.value=2200;
      this._g=ac.createGain(); this._g.gain.value=0;
      this._osc.connect(this._lp); this._osc2.connect(this._lp);
      this._lp.connect(this._g); this._g.connect(ac.destination);
      this._osc.start(); this._osc2.start();
    }
    if(this._ac&&this._ac.state==='suspended') this._ac.resume();
  }
  audioUpdate(freq, spin, dist){
    if(!this._ac||!this.audioOn) return;
    const t=this._ac.currentTime;
    this._osc.frequency.setTargetAtTime(freq,t,0.05);
    this._osc2.frequency.setTargetAtTime(freq*2.01,t,0.05);
    this._g.gain.setTargetAtTime(spin*0.028*clamp(30/dist,0.2,1.5),t,0.1);
  }
}
const _v=new THREE.Vector3();
