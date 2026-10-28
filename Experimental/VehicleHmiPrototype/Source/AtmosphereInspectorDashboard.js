import {BuildAtmosphereMediumLight} from './AtmosphereMediumLight.js';
import {BuildAtmosphereColumn} from './AtmosphereColumnInstrument.js';
import {ObservatorySlider} from './ObservatorySlider.js';
// Atmosphere-only observatory UI. Baking cards are supplied unchanged by the native panel builder.
import {El,Button,Diagram} from './NativeEnvironmentControls.js';
import {SkyLutPreview} from './SkyLutPreview.js';
import {ResolveSunTransmittance} from './AtmosphereIntegrator.js';
const Ranges={Rayleigh:[0,3],Mie:[0,6],Ozone:[0,4],RayleighHeight:[1000,16000],MieHeight:[500,8000],MieG:[-.95,.95],SkyScale:[0,3],HorizonGlow:[0,3],WhiteLine:[0,3],TierSamples:[8,64]};
export function SetAtmosphereInspectorValue(S,Key,Value){if(!Ranges[Key]||!Number.isFinite(Value))return false;const [Min,Max]=Ranges[Key];S[Key]=Math.max(Min,Math.min(Max,Key==='TierSamples'?Math.round(Value):Value));return true;}
export function AtmosphereDensityProfile(S,Altitude){return {Rayleigh:Math.exp(-Math.max(0,Altitude)/S.RayleighHeight),Mie:Math.exp(-Math.max(0,Altitude)/S.MieHeight)};}
let LastTransmissionKey,LastTransmission;
export function AtmosphereTransmission(S){const Key=JSON.stringify([S.SunElevation,S.SunAzimuth,S.Rayleigh,S.Mie,S.Ozone,S.RayleighHeight,S.MieHeight]);if(Key===LastTransmissionKey)return LastTransmission;LastTransmissionKey=Key;const E=S.SunElevation*Math.PI/180,A=S.SunAzimuth*Math.PI/180;return LastTransmission=ResolveSunTransmittance([Math.cos(E)*Math.cos(A),Math.cos(E)*Math.sin(A),Math.sin(E)],S);}
export function BuildAtmosphereInspectorDashboard(H,C,P,BuildBaking){
 const S=H.Settings;S.UseBakedAtmosphere??=true;P.classList.add('atmosphere-observatory');let Active='overview',Baked=false,Dead=false;C.Disposers??=[];C.Disposers.push(()=>Dead=true);
 const Bind=F=>{C.Bindings.push(F);C.Live.push(F);F();};const Change=(K,V)=>{if(SetAtmosphereInspectorValue(S,K,V)){C.Notify(K,S);C.Synchronise();}};
 const Root=El('div','co-root');P.appendChild(Root);const Header=El('header','co-header'),TitleRow=El('div','co-title-line');TitleRow.appendChild(El('h1','','Atmosphere'));const Power=Button(TitleRow,'',()=>{S.AtmosphereEnabled=!S.AtmosphereEnabled;C.Notify('AtmosphereEnabled',S);C.Synchronise();},'co-power');Power.setAttribute('aria-label','Toggle atmosphere');Bind(()=>{Power.textContent=S.AtmosphereEnabled?'● Live':'○ Off';Power.classList.toggle('is-off',!S.AtmosphereEnabled);Power.setAttribute('aria-pressed',String(!!S.AtmosphereEnabled));});Header.append(El('div','co-eyebrow','ENVIRONMENT  /  03'),TitleRow,El('p','co-subtitle','Scattering, density & the visible sky.'));Root.appendChild(Header);
 const Nav=El('nav','co-tabs');Nav.setAttribute('role','tablist');Nav.setAttribute('aria-label','Atmosphere inspector sections');Root.appendChild(Nav);const Panes={},Tabs={};
 for(const [Id,Name] of [['overview','Overview'],['medium','Medium'],['light','Light response'],['baking','Baking']]){const B=Button(Nav,Name,()=>Activate(Id),'co-tab');B.id='atmosphere-tab-'+Id;B.setAttribute('role','tab');B.setAttribute('aria-controls','atmosphere-pane-'+Id);const Pane=El('div','co-pane');Pane.id='atmosphere-pane-'+Id;Pane.setAttribute('role','tabpanel');Pane.setAttribute('aria-labelledby',B.id);Panes[Id]=Pane;Tabs[Id]=B;Root.appendChild(Pane);B.addEventListener('keydown',E=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(E.key))return;E.preventDefault();const Keys=Object.keys(Tabs),I=E.key==='Home'?0:E.key==='End'?Keys.length-1:(Keys.indexOf(Id)+(E.key==='ArrowRight'?1:-1)+Keys.length)%Keys.length;Activate(Keys[I]);Tabs[Keys[I]].focus();});}
 function Activate(Id){Active=Id;for(const K in Panes){Panes[K].hidden=K!==Id;Tabs[K].setAttribute('aria-selected',String(K===Id));Tabs[K].tabIndex=K===Id?0:-1;}if(Id==='baking'&&!Baked){Baked=true;BuildBaking(Panes.baking);}C.Synchronise();}
 function Box(Parent,Title,Index){const E=El('section','co-card at-instrument');E.dataset.instrument=Index.split(' / ')[1].toLowerCase().replaceAll(' ','-');const Top=El('div','co-card-heading');Top.append(El('span','',Title),El('span','co-index',Index));E.appendChild(Top);Parent.appendChild(E);return E;}
 function Caption(Parent,Text){const E=El('p','co-caption',Text);Parent.appendChild(E);return E;}
 function Readout(Parent,Label,Read,Unit=''){const E=El('div','co-readout'),V=El('strong');E.append(El('span','co-label',Label),V,El('small','',Unit));Parent.appendChild(E);Bind(()=>V.textContent=Read());return E;}
 function Field(Parent,Key,Label,Unit='',Step=.01,Scale=1){return ObservatorySlider(C,Parent,S,Key,Label,Ranges[Key],Change,Unit,Step,Scale);}
 function Toggle(Parent,Key,Label){const B=Button(Parent,'',()=>{S[Key]=!S[Key];C.Notify(Key,S);C.Synchronise();},'co-toggle'),State=El('span');B.append(El('span','',Label),State);Bind(()=>{State.textContent=S[Key]?'On ↗':'Off';B.classList.toggle('on',!!S[Key]);B.setAttribute('aria-pressed',String(!!S[Key]));});return B;}
 const Overview=Panes.overview;BuildAtmosphereColumn(C,Overview,S,AtmosphereDensityProfile);const Sky=Box(Overview,'Sky-view capture','GPU / SCATTERING');Sky.classList.add('at-atlas');const SkyHeading=El('div','at-preview-heading');Sky.appendChild(SkyHeading);Readout(SkyHeading,'Lookup revision',()=>H.Atmosphere.Revision??0,'');SkyHeading.appendChild(El('span','co-tag','GPU / RGBA16F'));
 const Preview=new SkyLutPreview(H.Atmosphere,{Mode:'Panorama'});const Canvas=Diagram(C,Sky,640,340,X=>{if(!Dead&&Active==='overview')Preview.Draw(X.X,640,340);},null,'Atmosphere scattering atlas · decoded GPU sky lookup');Canvas.classList.add('at-sky-image');
 // Diagram normally clears before drawing. Gate the whole callback so skipped ticks retain pixels.
 const DrawPreview=C.Live[C.Live.length-1];let LastPreviewDraw=-Infinity;
 const RefreshPreview=()=>{const Now=performance.now();if(Dead||Active!=='overview'||Now-LastPreviewDraw<250)return;LastPreviewDraw=Now;DrawPreview();};RefreshPreview.Static=false;
 C.Live[C.Live.length-1]=RefreshPreview;C.Bindings[C.Bindings.indexOf(DrawPreview)]=RefreshPreview;
 Caption(Sky,'Decoded sky-view GPU pixels · tone-mapped diagnostic, not a scene camera. Sun disc, stars, Moon and fog are excluded.');
 const Metrics=El('div','co-kpis');Overview.insertBefore(Metrics,Sky);Readout(Metrics,'Rayleigh',()=>S.Rayleigh.toFixed(2),'× molecules');Readout(Metrics,'Mie',()=>S.Mie.toFixed(2),'× aerosols');Readout(Metrics,'Ozone',()=>S.Ozone.toFixed(2),'× absorption');
 BuildAtmosphereMediumLight(C,S,Panes,{Bind,Field,Toggle,Caption,Change},AtmosphereDensityProfile,AtmosphereTransmission);
 const Footer=El('footer','co-footer');Footer.append(El('span','','ATMOSPHERIC SCATTERING'),El('span','','Settings apply live ↗'));Root.appendChild(Footer);Activate('overview');return Root;
}
