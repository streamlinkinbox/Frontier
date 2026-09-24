const $=id=>document.getElementById(id),canvas=$('paint');
const defaults={density:.8,diameter:.35,spread:.24,roughness:.065,coat:.23,tint:0,pearl:0,film:420,light:0,distance:.19};
const silver={name:'Silver',min:[.72,.77,.82],max:[.72,.77,.82],weight:1};
const rgb=[{name:'Red',min:[.42,.006,.004],max:[.95,.055,.018],weight:1},{name:'Green',min:[.004,.24,.025],max:[.035,.8,.15],weight:1},{name:'Blue',min:[.003,.025,.35],max:[.025,.18,.95],weight:1}];
let gl,program,shared,yaw=0,pigment=[.014,.045,.13],coatColour=[1,1,1],palette=[structuredClone(silver)],moving=false,dirty=true,frame=0;
const fromHex=s=>[1,3,5].map(i=>{const c=parseInt(s.slice(i,i+2),16)/255;return c<=.04045?c/12.92:Math.pow((c+.055)/1.055,2.4)});
const toHex=rgb=>'#'+rgb.map(c=>Math.round(255*(c<=.0031308?12.92*c:1.055*Math.pow(c,1/2.4)-.055)).toString(16).padStart(2,'0')).join('');
const vertex=`#version 300 es
precision highp float;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));gl_Position=vec4(p*2.0-1.0,0,1);}`;
const header=`#version 300 es
precision highp float;
precision highp int;
`;
function build(fragment){const p=gl.createProgram();for(const [type,source] of [[gl.VERTEX_SHADER,vertex],[gl.FRAGMENT_SHADER,header+shared+fragment]]){const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));gl.attachShader(p,s);gl.deleteShader(s)}gl.linkProgram(p);if(!gl.getProgramParameter(p,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(p));return p}
function labels(){for(const name of Object.keys(defaults)){const v=Number($(name).value);$(name+'-value').textContent=name==='density'?v.toFixed(1)+'×':name==='diameter'?v.toFixed(2)+' mm':name==='film'?v+' nm':name==='distance'?(v*100).toFixed(1)+' cm':name==='light'?(v*180/Math.PI).toFixed(0)+'°':v.toFixed(name==='roughness'?3:2)}$('density-number').value=$('density').value}
function paletteNote(){$('palette-note').textContent=palette.every(p=>p.weight===0)?'All shares are zero: using the silver fallback.':''}
function paletteRows(){
 $('flake-palette').replaceChildren();
 palette.forEach((entry,i)=>{
  const row=document.createElement('div');row.className='palette-row';row.dataset.index=i;
  row.innerHTML='<div class="palette-title"><span></span><button type="button" class="remove-flake" aria-label="Remove flake colour">×</button></div><div class="palette-range"><label>Range start<input type="color" class="flake-min"></label><label>Range end<input type="color" class="flake-max"></label></div><label class="share">Relative share<input type="number" class="flake-weight" min="0" max="1000" step="0.1"></label>';
  row.querySelector('.palette-title span').textContent=entry.name;
  for(const side of ['min','max']){const input=row.querySelector('.flake-'+side);input.value=toHex(entry[side]);input.setAttribute('aria-label',entry.name+' '+side+' colour');input.oninput=()=>{entry[side]=fromHex(input.value);entry.name='Colour '+(i+1);row.querySelector('.palette-title span').textContent=entry.name;dirty=true}}
  const weight=row.querySelector('.flake-weight');weight.value=entry.weight;weight.oninput=()=>{entry.weight=Math.min(1000,Math.max(0,Number(weight.value)||0));paletteNote();dirty=true};weight.onchange=()=>{weight.value=entry.weight};
  const remove=row.querySelector('.remove-flake');remove.disabled=palette.length===1;remove.onclick=()=>{palette.splice(i,1);paletteRows();dirty=true};
  $('flake-palette').append(row);
 });
 $('add-flake').disabled=palette.length>=8;paletteNote();
}
function render(){
 gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.viewport(0,0,canvas.width,canvas.height);gl.useProgram(program);
 gl.uniform2f(gl.getUniformLocation(program,'resolution'),canvas.width,canvas.height);
 gl.uniform3fv(gl.getUniformLocation(program,'pigment'),pigment);gl.uniform3fv(gl.getUniformLocation(program,'coatColour'),coatColour);
 for(const name of Object.keys(defaults))gl.uniform1f(gl.getUniformLocation(program,'u_'+name),Number($(name).value));
 const mins=new Float32Array(24),maxs=new Float32Array(24),weights=new Float32Array(8);
 palette.forEach((p,i)=>{mins.set(p.min,i*3);maxs.set(p.max,i*3);weights[i]=p.weight});
 gl.uniform1i(gl.getUniformLocation(program,'paletteCount'),palette.length);gl.uniform3fv(gl.getUniformLocation(program,'paletteMin[0]'),mins);gl.uniform3fv(gl.getUniformLocation(program,'paletteMax[0]'),maxs);gl.uniform1fv(gl.getUniformLocation(program,'paletteWeight[0]'),weights);
 gl.uniform1f(gl.getUniformLocation(program,'yaw'),yaw);gl.drawArrays(gl.TRIANGLES,0,3);frame++;window.paintFrames=frame;
}
function preset(name){
 $('density').value=name==='smooth'?0:name==='rgb'?4:.8;$('pearl').value=name==='pearl'?1:0;
 if(name==='rgb'){palette=structuredClone(rgb);coatColour=fromHex('#2a70ed');$('coat-colour').value='#2a70ed';$('tint').value=.35;paletteRows();document.querySelector('.controls').scrollTop=0}
 document.querySelectorAll('[data-preset]').forEach(b=>b.classList.toggle('active',b.dataset.preset===name));labels();dirty=true;
}
function reset(){document.querySelector('.controls').scrollTop=0;for(const [k,v] of Object.entries(defaults))$(k).value=v;pigment=[.014,.045,.13];$('pigment').value=toHex(pigment);coatColour=[1,1,1];$('coat-colour').value='#ffffff';palette=[structuredClone(silver)];paletteRows();yaw=0;moving=false;$('animate').textContent='Move light';$('animate').classList.remove('active');preset('metallic')}
function loop(time){if(moving){$('light').value=Math.sin(time*.00035)*.5;labels();dirty=true}if(dirty){render();dirty=false}requestAnimationFrame(loop)}
// Real GLSL float output compared against the native evaluator, including dense coloured populations.
async function probes(){if(!gl.getExtension('EXT_color_buffer_float'))throw Error('Float render target unavailable');const probe=build(`
layout(location=0) out vec4 result;
void main(){uint i=uint(gl_FragCoord.x);AutomotivePaintParameters p=AutomotivePaintDefaults();p.PearlWeight=i%32u>=16u?1.0:0.0;p.Density=i<32u?float(i%4u)*0.3:float(1u<<(i%5u));
if(i>=32u){p.CoatTint=vec3(.03,.2,.9);p.CoatTintStrength=.35;}
float footprint=i>=48u?.01:.00004;
AutomotiveFlakeSurface s=AutomotivePrepareFlakes(p,vec2(.0003*float(i),.00017*float(i)),vec2(footprint,0),vec2(0,footprint));
if(i>=32u)s=AutomotiveApplyFlakePalette(s,AutomotiveRgbPalette());
result=vec4(AutomotiveEvaluatePaint(p,s,normalize(vec3(.1,0,1)),normalize(vec3((float(i%32u)-16.0)*.025,.15,1))),1);}`);
 const tex=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,tex);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA32F,64,1,0,gl.RGBA,gl.FLOAT,null);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);const fb=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,fb);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,tex,0);if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Incomplete float framebuffer');gl.viewport(0,0,64,1);gl.useProgram(probe);gl.drawArrays(gl.TRIANGLES,0,3);const data=new Float32Array(256);gl.readPixels(0,0,64,1,gl.RGBA,gl.FLOAT,data);const error=gl.getError();gl.deleteFramebuffer(fb);gl.deleteTexture(tex);gl.deleteProgram(probe);render();if(error!==gl.NO_ERROR)throw Error('GL probe error '+error);return Array.from({length:64},(_,i)=>Array.from(data.slice(i*4,i*4+3)));
}
window.paintReady=(async()=>{try{
 gl=canvas.getContext('webgl2',{antialias:false,alpha:false,preserveDrawingBuffer:true});if(!gl)throw Error('WebGL 2 is unavailable. Native reference PNGs below remain available.');const response=await fetch('shared.glsl');if(!response.ok)throw Error('Shared shader could not be loaded');shared=await response.text();
 program=build(`
uniform vec2 resolution;uniform vec3 pigment,coatColour;uniform float yaw;
uniform float u_density,u_diameter,u_spread,u_roughness,u_coat,u_tint,u_pearl,u_film,u_light,u_distance;
uniform int paletteCount;uniform vec3 paletteMin[8],paletteMax[8];uniform float paletteWeight[8];
layout(location=0) out vec4 colour;
void main(){AutomotivePaintParameters p=AutomotivePaintDefaults();p.Pigment=pigment;p.Density=u_density;p.DiameterMm=u_diameter;p.NormalSpread=u_spread;p.FlakeRoughness=u_roughness;p.CoatRoughness=u_coat;p.CoatTint=coatColour;p.CoatTintStrength=u_tint;p.PearlWeight=u_pearl;p.FilmThicknessNm=u_film;
AutomotiveFlakePalette palette=AutomotivePaletteDefaults();palette.Count=paletteCount;for(int i=0;i<8;++i){palette.Minimum[i]=paletteMin[i];palette.Maximum[i]=paletteMax[i];palette.Weight[i]=paletteWeight[i];}
colour=vec4(APTonemap(APStudio(gl_FragCoord.xy,resolution,p,u_light,yaw,u_distance,palette)),1);}`);
 for(const name of Object.keys(defaults))$(name).oninput=()=>{labels();dirty=true};
 $('density-number').oninput=()=>{const v=Number($('density-number').value);if(Number.isFinite(v)){$('density').value=Math.max(0,Math.min(16,v));$('density-value').textContent=Number($('density').value).toFixed(1)+'×';dirty=true}};$('density-number').onchange=labels;
 $('pigment').value=toHex(pigment);$('pigment').oninput=()=>{pigment=fromHex($('pigment').value);dirty=true};
 $('coat-colour').oninput=()=>{coatColour=fromHex($('coat-colour').value);if(Number($('tint').value)===0)$('tint').value=.35;labels();dirty=true};
 $('add-flake').onclick=()=>{if(palette.length>=8)return;const entry=structuredClone(rgb[(palette.length-1)%rgb.length]);entry.name+=' '+(palette.length+1);palette.push(entry);paletteRows();dirty=true};
 document.querySelectorAll('[data-preset]').forEach(b=>b.onclick=()=>preset(b.dataset.preset));$('animate').onclick=()=>{moving=!moving;$('animate').textContent=moving?'Pause light':'Move light';$('animate').classList.toggle('active',moving)};$('reset').onclick=reset;
 let drag=null;canvas.onpointerdown=e=>{drag=e.clientX;canvas.setPointerCapture(e.pointerId)};canvas.onpointermove=e=>{if(drag!==null){yaw=Math.max(-.7,Math.min(.7,yaw+(e.clientX-drag)*.006));drag=e.clientX;dirty=true}};canvas.onpointerup=canvas.onpointercancel=()=>{drag=null};
 labels();paletteRows();render();$('state').textContent='Live WebGL 2 · coloured flake populations';window.paintProbes=probes;window.paintDraw=()=>{render();return frame};requestAnimationFrame(loop);return true;
}catch(e){$('state').textContent='Live shader unavailable';$('error').textContent=e.stack||e.message;window.paintFailure=e.message;return false}})();
