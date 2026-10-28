// One control for editor and environment panels. Label/value above a full-width track.
export const SliderDigits=(Min,Step)=>{const Places=V=>{const [M,E='0']=String(V).toLowerCase().split('e');return Math.max(0,(M.split('.')[1]?.length??0)-Number(E));};return Math.min(10,Math.max(Places(Min),Places(Step)));};
export function SnapSlider(V,Min,Max,Step){return Number(Math.max(Min,Math.min(Max,Min+Math.round((V-Min)/Step)*Step)).toFixed(SliderDigits(Min,Step)));}
export function ConstructSlider(Parent,Target,Key,O){
 const {Label,Minimum:Min,Maximum:Max,Step=.01,Scale=1,Unit='',Digits=SliderDigits(Min*Scale,Step*Scale)}=O;
 const Make=(Tag,Class,Text)=>{const E=document.createElement(Tag);E.className=Class;if(Text)E.textContent=Text;return E;};
 const Root=Make('div','frontier-slider ne-control'),Head=Make('div','frontier-slider-head'),Caption=Make('label','ne-control-label',Label),Value=Make('label','frontier-slider-value'),NumberInput=Make('input','ne-number'),Suffix=Make('span','ne-unit',Unit),Track=Make('input','frontier-slider-track ne-range');
 NumberInput.type='number';NumberInput.min=Min*Scale;NumberInput.max=Max*Scale;NumberInput.step=Step*Scale;NumberInput.setAttribute('aria-label',Label+' value');
 Track.type='range';Track.min=Min;Track.max=Max;Track.step=Step;Track.setAttribute('aria-label',Label);Value.append(NumberInput,Suffix);Head.append(Caption,Value);Root.append(Head,Track);Parent.appendChild(Root);
 const Refresh=()=>{const V=Number(Target[Key]??Min);Track.value=V;if(document.activeElement!==NumberInput)NumberInput.value=(V*Scale).toFixed(Digits);const Fill=`${Math.max(0,Math.min(100,(V-Min)/(Max-Min)*100))}%`;if(Root.style.setProperty)Root.style.setProperty('--slider-fill',Fill);else Root.style['--slider-fill']=Fill;};
 const Commit=V=>{if(!Number.isFinite(V))return;Target[Key]=SnapSlider(V,Min,Max,Step);O.OnInput?.(Target[Key]);Refresh();};
 Track.addEventListener('input',()=>Commit(+Track.value));
 const NumberCommit=()=>{if(NumberInput.value!=='')Commit(+NumberInput.value/Scale);};NumberInput.addEventListener('input',NumberCommit);NumberInput.addEventListener('change',NumberCommit);NumberInput.addEventListener('blur',()=>{NumberInput.value=(Number(Target[Key]??Min)*Scale).toFixed(Digits);});
 if(O.Labels){const Ends=Make('div','range-labels');Ends.append(Make('span','',O.Labels[0]),Make('span','',O.Labels[1]));Root.appendChild(Ends);}Refresh();return {Element:Root,Refresh};
}
