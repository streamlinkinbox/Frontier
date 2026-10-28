import {BuildPrecipitationInspector} from './PrecipitationSequence.js';
import {LightningEditorSettings,ApplyLightningEditor} from './LightningSettings.js';
import {SaveEnvironment} from './EnvironmentPersistence.js';
const Editors={particles:{Name:'Particle Editor',Url:'/Experimental/ParticleEditor/index.html'},flux:{Name:'Flux · Particle Studio',Url:'/reference-apps/Published/FluxParticleStudio/index.html'},fracture:{Name:'Fracture Editor',Url:'/reference-apps/Upstream/Exhibits/FractureEditor/index.html',Standalone:true}};
export const ParticleWorkspaceEditors=Editors;
export const WorkspaceStorageKey=Id=>'Frontier.ParticleWorkspace.'+Id+'.v1';
export function ValidateParticleDocument(Id,Doc){
 if(!Doc||typeof Doc!=='object')return false;
 try{const Text=JSON.stringify(Doc);if(Text.length>4_000_000)return false;}catch{return false;}
 if(Id==='particles')return Doc.Format==='frontier-particle-scene'&&Doc.Version===1&&Array.isArray(Doc.Systems)&&Doc.Systems.length<=32&&Doc.Systems.every(S=>S&&typeof S.Preset==='string'&&S.Settings&&typeof S.Settings==='object'&&!Array.isArray(S.Settings))&&(!Doc.Forces||Array.isArray(Doc.Forces)&&Doc.Forces.length<=32&&Doc.Forces.every(F=>F&&typeof F.Kind==='string'));
 if(Id==='flux')return Doc.format==='flux-comp'&&Doc.version===1&&!!Doc.comp&&Array.isArray(Doc.layers)&&Doc.layers.length>0&&Doc.layers.length<=24;
 return false;
}
export function ValidWorkspaceEnvelope(M,Token,Editor){return M?.Frontier==='particle-workspace'&&M.Version===1&&M.Token===Token&&M.Editor===Editor&&['ready','document','metrics','error','storage-error'].includes(M.Type);}
function Node(Tag,Text){const E=document.createElement(Tag);if(Text)E.textContent=Text;return E;}
export class ParticleWorkspace{
 constructor(Host){this.Host=Host;this.Documents={};this.DocumentRevisions={particles:0,flux:0};this.Active=false;this.Receive=this.Receive.bind(this);window.addEventListener('message',this.Receive);for(const Id of Object.keys(Editors))try{const D=JSON.parse(localStorage.getItem(WorkspaceStorageKey(Id))??'null');if(ValidateParticleDocument(Id,D))this.Documents[Id]=D;}catch{}}
 Event(Name,Data={}){this.Host.Telemetry?.Event(Name,{workspace:this.Editor,...Data});}
 SetStatus(Text){if(this.Status)this.Status.textContent=Text;}
 OpenLightning(){
  const Doc=this.Documents.particles??{Format:'frontier-particle-scene',Version:1,Systems:[{Preset:'strikeSparks',Name:'Strike sparks',Settings:{}}]};
  this.Documents.particles={...Doc,Lightning:LightningEditorSettings(this.Host.Settings),SelectionKind:'lightning'};
  if(this.Active&&this.Editor==='particles'){this.Send('restore',this.Documents.particles);return;}
  this.Open('particles');
 }
 ApplyLightning(){if(!this.Ready||this.Editor!=='particles'){this.SetStatus('Open the Particle Editor and wait until ready.');return;}this.Flush(()=>{
  if(!ApplyLightningEditor(this.Host.Settings,this.Documents.particles?.Lightning)){this.SetStatus('No lightning settings in this document.');return;}
  SaveEnvironment(this.Host.Settings);this.Host.Telemetry?.Event('lightning_editor_applied',{settings:LightningEditorSettings(this.Host.Settings)});this.Host.Telemetry?.Snapshot(true);this.SetStatus('Lightning shape & timing applied to scene · Back to scene → Clouds → Strike ahead');
 });}
 Open(Id='particles'){
  if(!Editors[Id])return;if(this.Active){if(Id!==this.Editor)this.Transition(Id);return;}
  this.Host.Lightning?.Audio.Stop();this.Host.Lightning?.Storm.Clear();this.Host.Shell?.Gizmo?.Cancel();this.Host.Keys?.clear();this.Active=true;this.Event('particle_workspace_open',{editor:Id});
  this.Root=Node('section');this.Root.className='particle-workspace';this.Root.setAttribute('aria-label','Particle and fracture authoring workspace');
  const Header=Node('header'),Brand=Node('strong','frontier. / Authoring'),Back=Node('button','← Back to scene');Back.onclick=()=>this.Transition(null);Header.append(Back,Brand);
  this.TabButtons={};for(const [Key,Info]of Object.entries(Editors)){const B=Node('button',Info.Name);B.onclick=()=>this.Transition(Key);B.setAttribute('role','tab');this.TabButtons[Key]=B;Header.appendChild(B);}
  const Precip=Node('button','Precipitation');Precip.onclick=()=>this.Transition('precipitation');Header.appendChild(Precip);
  const Apply=Node('button','Apply lightning to scene');Apply.onclick=()=>this.ApplyLightning();Header.appendChild(Apply);this.ApplyLightningButton=Apply;
  const Export=Node('button','Export project');Export.onclick=()=>this.Export();Header.appendChild(Export);this.ExportButton=Export;
  const Import=Node('button','Import project'),Input=Node('input');Input.type='file';Input.accept='.json,application/json';Input.hidden=true;Import.onclick=()=>Input.click();Input.onchange=async()=>{const F=Input.files?.[0];Input.value='';if(!F)return;try{if(F.size>8_000_000)throw new Error('Project exceeds 8 MB.');const D=JSON.parse(await F.text());this.Import(D);}catch(E){this.SetStatus('Import failed: '+E.message);}};Header.append(Import,Input);this.ImportButton=Import;
  this.Status=Node('span','Loading editor…');this.Status.setAttribute('role','status');Header.appendChild(this.Status);
  const Reload=Node('button','Reload tool');Reload.onclick=()=>{if(this.Ready)this.Transition(this.Editor);else this.Load(this.Editor);};Header.appendChild(Reload);
  this.Container=Node('div');this.Container.className='particle-workspace-frame';this.Root.append(Header,this.Container);document.body.appendChild(this.Root);this.Load(Id);
 }
 Load(Id){
  clearTimeout(this.PendingTimeout);this.Pending=null;this.Ready=false;this.Editor=Id;this.Token=globalThis.crypto?.randomUUID?.()??String(performance.now());
  this.Frame?.remove();this.Frame=Node('iframe');this.Frame.title=Editors[Id].Name;this.Frame.allow='clipboard-write; autoplay';
  const U=new URL(Editors[Id].Url,location.href);U.searchParams.set('workspace-token',this.Token);if(Id==='particles')U.searchParams.set('scene','sparks');if(Editors[Id].Standalone)this.Frame.onload=()=>{if(this.Editor!==Id)return;this.Ready=true;this.SetStatus('Standalone fracture tool · use its Export fracture button · no scene mesh handoff');};this.Frame.src=U.href;
  this.Container.appendChild(this.Frame);for(const [Key,B]of Object.entries(this.TabButtons)){B.classList.toggle('active',Key===Id);B.setAttribute('aria-selected',String(Key===Id));}
  if(this.ApplyLightningButton)this.ApplyLightningButton.hidden=Id!=='particles';
  if(this.ExportButton)this.ExportButton.hidden=!!Editors[Id].Standalone;if(this.ImportButton)this.ImportButton.hidden=!!Editors[Id].Standalone;
  this.SetStatus('Loading '+Editors[Id].Name+'…');this.Event('particle_workspace_tool',{editor:Id});
 }
 Send(Type,Payload,Request){this.Frame?.contentWindow?.postMessage({Frontier:'particle-workspace',Version:1,Token:this.Token,Type,Payload,Request},location.origin);}
 Receive(E){
  if(!this.Active||E.origin!==location.origin||E.source!==this.Frame?.contentWindow||!ValidWorkspaceEnvelope(E.data,this.Token,this.Editor))return;
  const M=E.data;
  if(M.Type==='ready'){this.Ready=true;this.Event('particle_document_loaded',{document_revision:this.DocumentRevisions[this.Editor],document:this.Documents[this.Editor]??null});this.Send('restore',this.Documents[this.Editor]??null);this.SetStatus('Ready · authored settings autosave locally');return;}
  if(M.Type==='document'){
   if(!ValidateParticleDocument(this.Editor,M.Payload)){this.SetStatus('Invalid editor document — not saved');return;}
   const Changed=JSON.stringify(this.Documents[this.Editor])!==JSON.stringify(M.Payload);
   this.Documents[this.Editor]=structuredClone(M.Payload);let Saved=true;try{localStorage.setItem(WorkspaceStorageKey(this.Editor),JSON.stringify(M.Payload));}catch{Saved=false;}
   this.SetStatus(Saved?'Saved locally · simulation restarts on reopen':'Saved in memory only — export before reloading');if(Changed){this.DocumentRevisions[this.Editor]=(this.DocumentRevisions[this.Editor]??0)+1;this.Event('particle_document_changed',{editor:this.Editor,document_revision:this.DocumentRevisions[this.Editor],document:this.Documents[this.Editor],systems:M.Payload.Systems?.length,layers:M.Payload.layers?.length});}
   if(this.Pending&&this.Pending.Request===M.Request){const Done=this.Pending.Done;clearTimeout(this.PendingTimeout);this.Pending=null;Done();}return;
  }
  if(M.Type==='metrics'){const P=M.Payload;if(P&&typeof P==='object'){const Values=Object.fromEntries(Object.entries(P).filter(([,V])=>typeof V==='number'&&Number.isFinite(V)||typeof V==='string'||typeof V==='boolean'));this.Host.Telemetry?.Add({kind:'particle_workspace',editor:this.Editor,document_revision:this.DocumentRevisions[this.Editor],...Values});}return;}
  this.SetStatus((M.Type==='error'?'Editor error: ':'Storage warning: ')+String(M.Payload).slice(0,300));this.Event('particle_workspace_error',{message:String(M.Payload).slice(0,1000)});
 }
 Flush(Done){
  if(Editors[this.Editor]?.Standalone){Done();return;}
  if(this.Pending)return;
  if(!this.Ready){Done();return;}
  const Request=globalThis.crypto?.randomUUID?.()??String(performance.now());this.Pending={Request,Done};this.SetStatus('Saving current edits…');this.Send('save',null,Request);
  this.PendingTimeout=setTimeout(()=>{this.Pending=null;this.SetStatus('Editor did not acknowledge save. Retry, or export from the editor before leaving.');},5000);
 }
 Transition(Id){this.Flush(()=>{if(Id==='precipitation'){this.Close();this.Host.Shell.Select('particles');this.Host.PrecipitationInspector?.scrollIntoView({block:'start'});}else if(Id)this.Load(Id);else this.Close();});}
 Close(){clearTimeout(this.PendingTimeout);this.Frame?.remove();this.Root?.remove();this.Frame=null;this.Root=null;this.Ready=false;this.Pending=null;this.Active=false;this.Host.Keys?.clear();this.Host.LastTime=performance.now();this.Host.PriorFrameTime=this.Host.LastTime;this.Event('particle_workspace_close');this.Host.Shell?.RefreshRows();}
 Export(){if(!this.Ready){this.SetStatus('Wait for the editor to finish loading before exporting.');return;}this.Flush(()=>{const D={Format:'frontier-particle-workspace',Version:1,Editors:this.Documents},U=URL.createObjectURL(new Blob([JSON.stringify(D,null,2)],{type:'application/json'})),A=Node('a');A.href=U;A.download='frontier-particles.json';A.click();setTimeout(()=>URL.revokeObjectURL(U),1000);this.SetStatus('Project exported · both particle tools included');});}
 Import(Doc){
  const Bundle=Doc?.Format==='frontier-particle-workspace'&&Doc.Version===1?Doc.Editors:Doc?.Format==='frontier-particle-scene'?{particles:Doc}:Doc?.format==='flux-comp'?{flux:Doc}:null;
  if(!Bundle||typeof Bundle!=='object'||!Object.keys(Bundle).length||Object.entries(Bundle).some(([Id,D])=>!ValidateParticleDocument(Id,D)))throw new Error('Unsupported or invalid particle project.');
  // Validate the entire import before changing either document, then flush current edits.
  this.Flush(()=>{for(const [Id,D]of Object.entries(Bundle)){this.Documents[Id]=structuredClone(D);this.DocumentRevisions[Id]=(this.DocumentRevisions[Id]??0)+1;this.Event('particle_document_imported',{editor:Id,document_revision:this.DocumentRevisions[Id],document:this.Documents[Id]});try{localStorage.setItem(WorkspaceStorageKey(Id),JSON.stringify(D));}catch{}}
   if(Bundle[this.Editor])this.Send('restore',this.Documents[this.Editor]??null);else this.Load(Object.keys(Bundle)[0]);this.Event('particle_project_imported');this.SetStatus('Project imported');});
 }
}
export function BuildParticleInspector(H,C,B){
 BuildPrecipitationInspector(H,C,B);
 const Card=C.Card(B,'Particle authoring','Sparkles',{Accent:'#b494f4',Index:2});
 C.Muted(Card,'The original Particle Editor and Flux Particle Studio, together in one authoring workspace. Edit real GPU particles using their original outliners, inspectors and previews.');
 C.Button(Card,'Open Precipitation component','Layers',()=>{H.PrecipitationInspector?.scrollIntoView({block:'start'});});
 C.Button(Card,'Open Particle Editor','Sparkles',()=>H.ParticleWorkspace.Open('particles'));
 C.Button(Card,'Author lightning → apply to scene','Sparkles',()=>H.ParticleWorkspace.OpenLightning());
 C.Button(Card,'Open Flux Particle Studio','Layers',()=>H.ParticleWorkspace.Open('flux'));
 C.Button(Card,'Open Fracture Editor','Layers',()=>H.ParticleWorkspace.Open('fracture'));
 C.Muted(Card,'Fracture opens the supplied original editor with its own geometry generation, local storage and Export fracture controls. It does not yet replace driving-scene meshes.');
 C.Readout(Card,()=>{const D=H.ParticleWorkspace.Documents;return [`Particle scene: ${D.particles?.Systems.length??0} saved systems`,`Flux composition: ${D.flux?.layers.length??0} saved layers`];});
 C.Muted(Card,'The driving scene pauses while authoring. Documents autosave locally and can be imported/exported together. Lightning shape/timing can now be explicitly applied to the scene. Other emitters still render only in their authoring previews. Existing Derez effects remain unchanged.');
}
