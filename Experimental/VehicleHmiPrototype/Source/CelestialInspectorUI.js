import {El, Button, Diagram} from './NativeEnvironmentControls.js';
import {ObservatorySlider} from './ObservatorySlider.js';

// Shared accessible controls only. Each celestial inspector supplies its own
// infographic geometry, layout and data model, not a generic card template.
export function CelestialInspectorUI(C, P, {Title, Subtitle, Theme, Tabs}) {
    P.classList.add('celestial-observatory', Theme);
    let Alive = true;
    C.Disposers.push(() => Alive = false);
    const Root = El('div', 'ci-root');
    const Header = El('header', 'ci-header');
    Header.append(El('span', 'ci-eyebrow', 'ENVIRONMENT / CELESTIAL'), El('h1', '', Title), El('p', '', Subtitle));
    Root.appendChild(Header);
    P.appendChild(Root);
    const Nav = El('nav', 'ci-tabs');
    Nav.setAttribute('role', 'tablist');
    Nav.setAttribute('aria-label', Title + ' inspector sections');
    Root.appendChild(Nav);
    const Panes = {}, Buttons = {}, Hooks = {};
    for (const [Index, [Id, Label]] of Tabs.entries()) {
        const Pane = El('div', 'ci-pane');
        Pane.id = Theme + '-' + Id;
        Pane.hidden = Index !== 0;
        Pane.setAttribute('role', 'tabpanel');
        const B = Button(Nav, Label, () => Activate(Id), 'ci-tab');
        B.id = Theme + '-tab-' + Id;
        B.setAttribute('role', 'tab');
        B.setAttribute('aria-controls', Pane.id);
        Pane.setAttribute('aria-labelledby', B.id);
        Buttons[Id] = B;
        Panes[Id] = Pane;
        Root.appendChild(Pane);
        B.addEventListener('keydown', E => {
            if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(E.key)) return;
            E.preventDefault();
            const Keys = Object.keys(Panes), I = Keys.indexOf(Id);
            const Next = E.key === 'Home' ? 0 : E.key === 'End' ? Keys.length - 1 : (I + (E.key === 'ArrowRight' ? 1 : -1) + Keys.length) % Keys.length;
            Activate(Keys[Next]);
            Buttons[Keys[Next]].focus();
        });
    }
    function Activate(Id) {
        for (const K in Panes) {
            Panes[K].hidden = K !== Id;
            Buttons[K].setAttribute('aria-selected', String(K === Id));
            Buttons[K].tabIndex = K === Id ? 0 : -1;
        }
        Hooks[Id]?.();
        C.Synchronise();
    }
    function Bind(F, Live = false) {
        F.Static = !Live;
        C.Bindings.push(F);
        C.Live.push(F);
        F();
    }
    function Section(Parent, Title, Detail, Class = '') {
        const E = El('section', 'ci-section ' + Class), Head = El('div', 'ci-heading');
        Head.append(El('h2', '', Title), El('span', '', Detail));
        E.appendChild(Head);
        Parent.appendChild(E);
        return E;
    }
    function Note(Parent, Text) { const E = El('p', 'ci-note', Text); Parent.appendChild(E); return E; }
    function Metric(Parent, Label, Read, Unit = '') {
        const E = El('div', 'ci-metric'), V = El('strong');
        E.append(El('span', '', Label), V, El('small', '', Unit));
        Parent.appendChild(E);
        Bind(() => V.textContent = Read());
        return E;
    }
    function Toggle(Parent, Target, Key, Label) {
        const B = Button(Parent, '', () => {Target[Key] = !Target[Key]; C.Notify(Key, Target); C.Synchronise();}, 'ci-switch');
        B.setAttribute('aria-label', Label);
        B.append(El('span', '', Label), El('i'));
        Bind(() => {B.classList.toggle('on', !!Target[Key]); B.setAttribute('aria-pressed', String(!!Target[Key]));});
        return B;
    }
    function Field(Parent, Target, Key, Label, Range, Commit, Unit = '', Step = .01, Scale = 1) {
        return ObservatorySlider(C, Parent, Target, Key, Label, Range, Commit, Unit, Step, Scale);
    }
    function Plot(Parent, W, H, Key, Draw, Drag, Label, Dynamic = false, MaxHz = Infinity) {
        const Canvas = Diagram(C, Parent, W, H, Draw, Drag, Label);
        const Redraw = C.Live[C.Live.length - 1];
        let Last, LastDraw = -Infinity;
        const Update = () => {
            if (!Alive || Parent.closest?.('[hidden]')) return;
            const Next = JSON.stringify(Key());
            if (Next === Last || performance.now() - LastDraw < 1000 / MaxHz) return;
            LastDraw = performance.now();
            Last = Next;
            Redraw();
        };
        Update.Static = !Dynamic && MaxHz === Infinity;
        C.Live[C.Live.length - 1] = Update;
        C.Bindings[C.Bindings.indexOf(Redraw)] = Update;
        return Canvas;
    }
    Activate(Tabs[0][0]);
    return {Root, Header, Panes, Hooks, Activate, Bind, Section, Note, Metric, Toggle, Field, Plot, IsAlive: () => Alive};
}
