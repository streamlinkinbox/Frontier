// ═══════════════════════════════════════════════════════════════════
// ui.js — cinematic camera + HUD for the tick lab
// (Ticks are silent — no whine synth. Haller's organ does the hearing.)
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';

const clamp=THREE.MathUtils.clamp, lerp=THREE.MathUtils.lerp;
const damp=(a,b,l,dt)=>lerp(a,b,1-Math.exp(-l*dt));

export class Camera{
  constructor(canvas,world){
    this.canvas=canvas; this.world=world;
    this.cam=new THREE.PerspectiveCamera(42,1,2,9000);
    this.mode='track';
    this.target=new THREE.Vector3(250,60,430); this.goalT=this.target.clone();
    this.yaw=2.6; this.pitch=0.35; this.dist=420;
    this.gYaw=this.yaw; this.gPitch=this.pitch; this.gDist=this.dist;
    this.cineA=0;
    this._bind(); this.resize();
  }
  resize(){
    const w=this.canvas.clientWidth||innerWidth, h=this.canvas.clientHeight||innerHeight;
    this.cam.aspect=w/h; this.cam.updateProjectionMatrix();
  }
  _bind(){
    const c=this.canvas;
    let drag=null, pinch=0;
    c.addEventListener('contextmenu',e=>e.preventDefault());
    c.addEventListener('pointerdown',e=>{ c.setPointerCapture(e.pointerId);
      drag={x:e.clientX,y:e.clientY,b:e.button}; });
    c.addEventListener('pointermove',e=>{
      if(!drag) return;
      const dx=e.clientX-drag.x, dy=e.clientY-drag.y;
      drag.x=e.clientX; drag.y=e.clientY;
      if(drag.b===2){
        const s=this.dist*0.0016;
        const fwd=new THREE.Vector3(); this.cam.getWorldDirection(fwd);
        const right=new THREE.Vector3().crossVectors(fwd,this.cam.up).normalize();
        const up=new THREE.Vector3().crossVectors(right,fwd).normalize();
        this.goalT.addScaledVector(right,-dx*s).addScaledVector(up,dy*s);
        if(this.mode!=='free') this.setMode('free',true);
      } else {
        this.gYaw-=dx*0.0052; this.gPitch=clamp(this.gPitch+dy*0.0052,-1.2,1.45);
      }
    });
    const up=()=>{ drag=null; };
    c.addEventListener('pointerup',up); c.addEventListener('pointercancel',up);
    c.addEventListener('wheel',e=>{ e.preventDefault();
      this.gDist=clamp(this.gDist*(1+Math.sign(e.deltaY)*0.1),30,2600);
    },{passive:false});
    c.addEventListener('touchmove',e=>{
      if(e.touches.length===2){ e.preventDefault();
        const d=Math.hypot(e.touches[0].clientX-e.touches[1].clientX,
                            e.touches[0].clientY-e.touches[1].clientY);
        if(pinch) this.gDist=clamp(this.gDist*(pinch/d),30,2600);
        pinch=d;
      }
    },{passive:false});
    c.addEventListener('touchend',()=>pinch=0);
  }
  setMode(m,silent){
    this.mode=m;
    if(m==='macro') this.gDist=120;
    if(m==='track'&&this.gDist<100) this.gDist=380;
    if(!silent&&this.onMode) this.onMode(m);
  }
  update(dt,focus,headPos){
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
    if(_p.y<8)_p.y=8;
    this.cam.position.copy(_p);
    this.cam.lookAt(this.target);
  }
}
const _p=new THREE.Vector3();

const $=id=>document.getElementById(id);
export class HUD{
  constructor(){
    this.labelsOn=true;
    this.logEl=$('log');
    this.labelDefs=[
      ['scutum','scutum (never expands)'],['alloscutum','alloscutum sac'],
      ['capitulum','capitulum'],['hypostome','barbed hypostome','amber'],
      ['chelicera','chelicera (cutting)','amber'],['palp','palp · 4 articles'],
      ['haller',"Haller's organ",'amber'],['cement','cement cone','amber'],
      ['festoon','festoon'],['spiracle','spiracle plate'],
      ['coxa','coxa + spur'],['claw','claws + pulvillus'],
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
    btn('btn-co2',()=>this.cb.co2?.());
    btn('btn-breach-a',()=>this.cb.breachA?.());
    btn('btn-breach-b',()=>this.cb.breachB?.());
    btn('btn-drop',()=>this.cb.drop?.());
    btn('btn-reset',()=>this.cb.reset?.());
    btn('btn-guide',()=>$('guide').classList.remove('hidden'));
    btn('btn-guide-close',()=>$('guide').classList.add('hidden'));
    $('guide').addEventListener('click',e=>{ if(e.target.id==='guide')e.target.classList.add('hidden'); });
    const tg=(id,fn)=>$(id).addEventListener('click',()=>{
      $(id).classList.toggle('on'); fn($(id).classList.contains('on')); });
    tg('tg-labels',on=>{ this.labelsOn=on; });
    tg('tg-demo',on=>this.cb.demo?.(on));
    $('sl-speed').addEventListener('input',e=>{
      const v=e.target.value/100, s=(10+v*110).toFixed(0);
      $('v-speed').textContent=`${s} mm/s`; this.cb.speed?.(10+v*110);
    });
    $('sl-time').addEventListener('input',e=>{
      const v=e.target.value/100, ts=Math.pow(10,-2.3+2.3*v);
      $('v-time').textContent=`${ts<0.1?ts.toFixed(3):ts.toFixed(2)}×`;
      this.cb.time?.(ts);
    });
  }
  on(evt,fn){ this.cb[evt]=fn; }
  setState(s,cls=''){ const b=$('state-badge'); b.textContent=s; b.className='state-badge '+cls; }
  log(msg){
    const t=new Date(), p=n=>String(n).padStart(2,'0');
    this.logEl.innerHTML=`<span class="t">${p(t.getMinutes())}:${p(t.getSeconds())}</span>${msg}`;
  }
  chips(gait,cement,host,fps){
    $('chip-gait').textContent=gait;
    $('chip-cement').textContent=cement;
    $('chip-host').textContent=host;
    $('chip-fps').textContent=fps.toFixed(0);
  }
  gauges(fa,fb,batt){
    $('g-fill-a').style.width=`${fa*100}%`; $('v-fill-a').textContent=`${(fa*100).toFixed(0)}%`;
    $('v-sub-a').textContent=fa<0.02?'unfed':fa<0.6?'feeding':fa<0.95?'engorging':'REPLETE';
    $('g-fill-b').style.width=`${fb*100}%`; $('v-fill-b').textContent=`${(fb*100).toFixed(0)}%`;
    $('v-sub-b').textContent=fb<0.02?'unfed':fb<0.6?'feeding':fb<0.95?'engorging':'REPLETE';
    $('g-batt').style.width=`${batt*100}%`; $('v-batt').textContent=`${(batt*100).toFixed(0)}%`;
  }
  tele(o){
    $('t-speed').textContent=`${o.speed.toFixed(0)} mm/s`;
    $('t-alt').textContent=`${o.alt.toFixed(0)} mm`;
    $('t-surf').textContent=o.surf;
    $('t-grip').textContent=o.grip;
    $('t-duty').textContent=o.duty;
    $('t-phase').textContent=o.phase;
  }
  steps(active){
    const order=['quest','breach','attach','cement','feed','replete'];
    document.querySelectorAll('#feed-steps li').forEach(li=>{
      const i=order.indexOf(li.dataset.s), a=order.indexOf(active);
      li.className=a<0?'':(i<a?'done':(i===a?'active':''));
    });
  }
  projectLabels(cam,anchors){
    for(const [key] of this.labelDefs){
      const el=this.labelEls[key], p=anchors[key];
      if(!p||!this.labelsOn){ el.classList.remove('show'); continue; }
      _v.copy(p).project(cam);
      if(_v.z>1){ el.classList.remove('show'); continue; }
      el.style.left=`${(_v.x*0.5+0.5)*innerWidth+14}px`;
      el.style.top=`${(-_v.y*0.5+0.5)*innerHeight-10}px`;
      el.classList.add('show');
    }
  }
}
const _v=new THREE.Vector3();
