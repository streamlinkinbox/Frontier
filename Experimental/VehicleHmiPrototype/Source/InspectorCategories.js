import {El,Button} from './NativeEnvironmentControls.js';

// Reusable accessible navigation, not a reusable infographic/card layout.
export function InspectorCategories(C,Parent,Entries,Prefix){
    const Nav=El('nav','inspector-categories');
    Nav.setAttribute('role','tablist');Nav.setAttribute('aria-label',Prefix+' inspector sections');
    Parent.appendChild(Nav);
    const Panes={},Buttons={};
    for(const [I,[Key,Name]]of Entries.entries()){
        const Pane=El('div','inspector-category-pane');Pane.id=Prefix+'-pane-'+Key;Pane.hidden=I!==0;Pane.setAttribute('role','tabpanel');
        const B=Button(Nav,Name,()=>Activate(Key),'inspector-category');B.id=Prefix+'-tab-'+Key;B.setAttribute('role','tab');B.setAttribute('aria-controls',Pane.id);Pane.setAttribute('aria-labelledby',B.id);
        Panes[Key]=Pane;Buttons[Key]=B;Parent.appendChild(Pane);
        B.addEventListener('keydown',E=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(E.key))return;E.preventDefault();const Keys=Object.keys(Panes),Index=E.key==='Home'?0:E.key==='End'?Keys.length-1:(Keys.indexOf(Key)+(E.key==='ArrowRight'?1:-1)+Keys.length)%Keys.length;Activate(Keys[Index]);Buttons[Keys[Index]].focus();});
    }
    function Activate(Key){for(const K in Panes){Panes[K].hidden=K!==Key;Buttons[K].setAttribute('aria-selected',String(K===Key));Buttons[K].tabIndex=K===Key?0:-1;}C.Synchronise();}
    Activate(Entries[0][0]);
    return {Panes,Activate,Nav};
}
