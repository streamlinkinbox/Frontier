// Shared input behaviour, not a shared card design: editable numbers above a wide drag track.
import {ConstructSlider} from './SliderControl.js';
export function ObservatorySlider(C,Parent,S,Key,Label,Range,Change,Unit='',Step=.01,Scale=1){
 const Control=ConstructSlider(Parent,S,Key,{Label,Minimum:Range[0],Maximum:Range[1],Step:Step/Scale,Scale,Unit,OnInput:Value=>Change(Key,Value)});
 Control.Element.classList.add('co-slider');
 const NumberInput=Control.Element.querySelector('input[type=number]'),Track=Control.Element.querySelector('input[type=range]');
 NumberInput.setAttribute('aria-label',Label);Track.setAttribute('aria-label',Label+' slider');
 Control.Refresh.Static=true;C.Bindings.push(Control.Refresh);C.Live.push(Control.Refresh);return NumberInput;
}
