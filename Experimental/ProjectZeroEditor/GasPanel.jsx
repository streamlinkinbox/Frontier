//============================================================================================================================================
//                                                              GASPANEL.JSX
//============================================================================================================================================
// 📦 The gas domain as the inspector shows it — a transform, its children, the few settings worth having inline, and one button to the full editor.
//
// Four components, and the split is the whole design:
//
//    GasPreview      a cheap stand-in for the volume that refuses to animate a domain the game would not run.
//    GasInspector    the domain card stack: transform, hierarchy, run policy, budget, eleven settings, ↗.
//    GasEmitterInspector   a child emitter, which is its own entity with its own transform.
//    GasEditor       the full page, opened from ↗, exactly as the wind card opens WindEditor. It is not a
//                    second design: it hosts the Fluid simulator page that already exists in Experimental/Fluid.
//
// 🔴 EVERY CONTROL HERE IS THE EDITOR'S OWN CONTROL.
//    Card, Control, Tile, Metric and TransformPanel are imported from the inspector rather than restyled
//    here, so a gas slider is the same object as a fog slider — same pill, same fill, same split value, same
//    keyboard behaviour — and stays that way the next time the editor's styling moves. The first draft of
//    this panel hand-rolled its rows and read as a visitor from another application, which it was. The only
//    CSS this file still owns is the preview canvas, the policy description, the child list and the drawer:
//    things the shared vocabulary has no word for.
//
// 🔴 THE PREVIEW REFUSES TO SHOW A PLUME THAT THE GAME WOULD NOT SHOW.
//    A dormant domain draws its bounds and nothing else. It would be easy — and much prettier — to animate
//    every card, and it would teach everyone that effects run by themselves. They do not: the budget admits
//    twelve live domains and most of a level's gas is dormant until something fires it. A preview that lies
//    about that is worse than no preview.
//
// ⚠️ The import of Card/Control from Inspectors.jsx closes a cycle, and it is the cycle Notch.jsx already
//    lives in. Both sides export hoisted function declarations and nothing is called at module scope, so
//    the binding is resolved at render time, long after both modules have finished evaluating.

import React, { useEffect, useRef, useState } from "react";
import { Card, Control, Glyph, Icon, Metric, Tile } from "./Inspectors.jsx";
import TransformPanel from "./TransformPanel.jsx";
import {
  ApplyGasPreset,
  GasEmitterRowSummary,
  GasEmitterSheet,
  GasEmitterTransformRows,
  GasEmitterToParticles,
  ParticleSettingsFromEmitter,
  EmitterValuesFromParticles,
  GasFadeSeconds,
  GasResidency,
  GasRetires,
  GasPresetChoices,
  GasPresetOptions,
  GasRowSummary,
  GasRunPolicies,
  GasRunning,
  GasSheet,
  GasSummary,
  GasTiers,
  GasTransformRows,
  NewGasEmitter,
  ResolveGas,
  SpellBytes,
  TierForDistance,
} from "./GasSpecification.js";
import "./GasPanel.css";

export { GasRowSummary, GasEmitterRowSummary, NewGasEmitter };

// Renders one sheet field through the editor's Control, which is the point of writing the sheet in the
//    editor's shape in the first place.
function Reader(Sheet, Values, Change) {
  return (...Labels) =>
    Labels.map((Label) => {
      const Field = Sheet.find((Item) => Item.Label === Label);
      if (!Field) return null;
      return (
        <Control
          key={Label}
          Field={Field}
          Value={Values[Label] ?? Field.Default}
          Change={(Next) => Change(Label, Next)}
        />
      );
    });
}

// A Select in a sheet carries indices; these domains carry identifiers. One place converts, rather than
//    every call site remembering to.
function Chooser({ Label, Choices, Current, Change }) {
  const Index = Math.max(
    0,
    Choices.findIndex((Choice) => Choice.Id === Current),
  );
  return (
    <Control
      Field={{ Label, Control: "Select", Options: Choices.map((Choice) => Choice.Name) }}
      Value={Index}
      Change={(Next) => Change(Choices[Next].Id)}
    />
  );
}

// ─── The preview ───────────────────────────────────────────────────────────────────────────────────────────

// A cheap 2D stand-in for the volume, drawn the way the real one is lit: smoke that scatters, heat that
//    emits, both rising. It is not the simulator and is not pretending to be — it reads the same settings so
//    that turning Fire down darkens this, which is what a preview is for.
export function GasPreview({ Resolved, Active = true, Playing = true, Running = true }) {
  const Canvas = useRef(null),
    Latest = useRef({});
  Latest.current = { Resolved, Active, Playing, Running };

  useEffect(() => {
    const Node = Canvas.current,
      Context = Node.getContext("2d");
    let Frame,
      Before = 0,
      Time = 0;
    // Deterministic seeds: the same card always draws the same plume, so a screenshot is comparable.
    const Puffs = Array.from({ length: 34 }, (_, Index) => ({
      Phase: ((Index * 61) % 97) / 97,
      Sway: (((Index * 37) % 71) / 71 - 0.5) * 2,
      Size: 0.45 + (((Index * 53) % 83) / 83) * 0.8,
    }));

    const Paint = (Now) => {
      Frame = requestAnimationFrame(Paint);
      if (Now - Before < 32 || !Node.getClientRects().length || document.hidden) return;
      const Step = Math.min(0.05, (Now - (Before || Now)) / 1000);
      Before = Now;
      const { Resolved, Active, Playing, Running } = Latest.current;
      if (Playing && Active && Running) Time += Step;

      const Box = Node.getBoundingClientRect(),
        W = Box.width,
        H = Box.height,
        Ratio = Math.min(2, devicePixelRatio || 1);
      if (!W || !H) return;
      if (Node.width !== Math.round(W * Ratio) || Node.height !== Math.round(H * Ratio)) {
        Node.width = Math.round(W * Ratio);
        Node.height = Math.round(H * Ratio);
      }
      Context.setTransform(Ratio, 0, 0, Ratio, 0, 0);
      Context.fillStyle = "#0e1316";
      Context.fillRect(0, 0, W, H);

      // The bounds box, always. It is the part of a domain that exists whether or not anything is simulating.
      const Settings = Resolved.Settings,
        Aspect = Settings.boundsHeight / Math.max(0.1, Settings.boundsWidth),
        BoxH = Math.min(H - 26, (W - 60) * Aspect),
        BoxW = BoxH / Math.max(0.2, Aspect),
        Left = (W - BoxW) / 2,
        Floor = H - 13;
      Context.strokeStyle = Running ? "#5e7f8c" : "#3a4950";
      Context.setLineDash(Running ? [] : [3, 4]);
      Context.lineWidth = 1;
      Context.strokeRect(Left, Floor - BoxH, BoxW, BoxH);
      Context.setLineDash([]);

      if (!Running) {
        Context.fillStyle = "#6c7d85";
        Context.font = "10px ui-monospace, monospace";
        Context.textAlign = "center";
        Context.fillText("DORMANT · NOT SIMULATING", W / 2, Floor - BoxH / 2);
        return;
      }

      // Smoke and heat. Extinction darkens, albedo lightens, fire intensity and emitter temperature set how
      //    much of the base is incandescent — the same four readings the raymarch gives the most weight to.
      const Heat = Math.min(1, (Settings.fireIntensity / 10) * Math.min(1, Settings.emitterTemperature / 4)),
        Albedo = Math.min(1, Settings.smokeAlbedo),
        Thickness = Math.min(1, Settings.densityExtinction / 40),
        Rate = Math.max(0.05, Settings.emitterRate),
        Rise = Math.max(0.2, Settings.buoyancy / 6);
      Context.globalCompositeOperation = "lighter";
      for (const Puff of Puffs) {
        const Life = (Time * 0.32 * Rise + Puff.Phase) % 1,
          Y = Floor - Life * BoxH * 0.96,
          Spread = 0.1 + Life * 0.46,
          X = Left + BoxW / 2 + Puff.Sway * Spread * BoxW * 0.42,
          Radius = Puff.Size * BoxW * (0.07 + Spread * 0.2),
          Fade = Math.sin(Math.PI * Math.min(1, Life * 1.25)) * Rate;
        const Glow = Math.max(0, Heat * (1 - Life * 3.2));
        const Shade = Context.createRadialGradient(X, Y, 0, X, Y, Math.max(1, Radius));
        const Grey = Math.round(70 + Albedo * 150);
        Shade.addColorStop(
          0,
          `rgba(${Math.round(Grey + Glow * (255 - Grey))},${Math.round(Grey + Glow * (150 - Grey))},${Math.round(Grey + Glow * (60 - Grey))},${0.1 + Fade * 0.3 * (0.35 + Thickness)})`,
        );
        Shade.addColorStop(1, "rgba(0,0,0,0)");
        Context.fillStyle = Shade;
        Context.beginPath();
        Context.arc(X, Y, Math.max(1, Radius), 0, Math.PI * 2);
        Context.fill();
      }
      Context.globalCompositeOperation = "source-over";
    };

    Frame = requestAnimationFrame(Paint);
    return () => cancelAnimationFrame(Frame);
  }, []);

  return (
    <div className="gas-visual">
      <canvas ref={Canvas} aria-label="Gas domain preview" />
    </div>
  );
}

// ─── The domain card stack ─────────────────────────────────────────────────────────────────────────────────

export function GasInspector({
  Values,
  Change,
  Open,
  Hidden,
  ToggleHidden,
  Children = [],
  SelectChild,
  AddEmitter,
}) {
  const [Fired, Fire] = useState(false);
  const Resolved = ResolveGas(Values, Children);
  const Running = !Hidden && GasRunning(Resolved, Fired);
  const F = Reader(GasSheet, Values, Change);
  const Amber = Resolved.Bytes > 96 * 1024 * 1024;

  // What the author has moved away from the preset, counted over the sheet only — a transform is not an
  //    override, because the preset never had an opinion about where the effect is.
  const Clean = ApplyGasPreset(Resolved.Preset, {});
  const Departures = GasSheet.filter(
    (Field) => Values[Field.Label] !== undefined && Values[Field.Label] !== Clean[Field.Label],
  );

  const Revert = () => {
    for (const Field of Departures) Change(Field.Label, Clean[Field.Label]);
  };

  const Pin = [{ Id: "auto", Name: "Automatic · by distance" }].concat(
    GasTiers.map((Tier) => ({
      Id: Tier.Id,
      Name: `${Tier.Name} · ${Tier.Extent ? Tier.Extent + "³" : "flipbook"}`,
    })),
  );

  return (
    <>
      <div className="tiles">
        <Tile Label="Visible" Context="gas" IconName="visible" On={!Hidden} Action={ToggleHidden} />
        <Tile
          Label="Obstructs"
          Context="gas"
          On={Values.Obstructs !== false}
          Action={() => Change("Obstructs", Values.Obstructs === false)}
        />
        <Tile
          Label="Pushes objects"
          Context="gas"
          On={!!Values.Coupled}
          Action={() => Change("Coupled", !Values.Coupled)}
        />
      </div>

      {/* ① The transform, first, because this is a 3D entity before it is an effect. Bounds take the row
          Scale occupies on a mesh: the domain's extent is its scale, in metres, and is not a child. */}
      <TransformPanel Values={Values} Change={Change} Rows={GasTransformRows} Space="WORLD SPACE" />
      <div className="section-caption">
        ROTATION ORIENTS CHILDREN, NOT THE LATTICE — the solver cube is axis-aligned
      </div>

      <Card Title="Domain">
        <GasPreview Resolved={Resolved} Running={Running} />
        <div className={"gas-state " + (Running ? "live" : "idle")}>
          <i />
          <span>{Hidden ? "Hidden · nothing simulates" : GasSummary(Resolved)}</span>
        </div>
        <Chooser
          Label="Preset"
          Choices={GasPresetChoices.map((Entry, Index) => ({
            Id: Entry.Id,
            Name: GasPresetOptions[Index],
          }))}
          Current={Resolved.Preset}
          Change={(Id) => {
            const Next = ApplyGasPreset(Id, Values);
            for (const [Key, Reading] of Object.entries(Next)) Change(Key, Reading);
          }}
        />
        {F("Dynamic Bounds", "Surge Limit", "Enclosed Box")}
        <p className="gas-note">
          Dynamic bounds let the cube grow to the surge limit when the plume reaches a wall, at the cost of
          coarser voxels for the same count. Enclosed stops it venting through the sides.
        </p>
      </Card>

      <Card Title="Simulation">
        <Chooser
          Label="Runs"
          Choices={GasRunPolicies}
          Current={Resolved.Policy.Id}
          Change={(Id) => Change("Run", Id)}
        />
        <p className="gas-note">{Resolved.Policy.Description}</p>
        {Resolved.Policy.Id === "triggered" && (
          <Control
            Field={{ Label: "Lifetime", Control: "Slider", Minimum: 0.2, Maximum: 30, Decimals: 1, Unit: "s", Default: 4 }}
            Value={Values.Lifetime ?? 4}
            Change={(Next) => Change("Lifetime", Next)}
          />
        )}
        {Resolved.Policy.Id === "triggered" && (
          <>
            <Control
              Field={{ Label: "Retire When Finished", Control: "Switch", Default: true }}
              Value={Values.Retire !== false}
              Change={(Next) => Change("Retire", Next)}
            />
            <p className="gas-note">
              {GasRetires(Resolved)
                ? `Runs ${Number(Values.Lifetime ?? 4).toFixed(1)} s, dissipates for ${GasFadeSeconds} s more, then the domain is destroyed and ${SpellBytes(GasResidency(Resolved).Released)} of velocity and smoke goes back to the budget. The row stays, dormant and re-firable.`
                : "Kept resident after it finishes. Re-fires without a rebuild, and holds its fields for the rest of the level — worth it for something that fires repeatedly, a leak with a schedule for anything else."}
            </p>
          </>
        )}
        {Resolved.Policy.Id !== "always" && (
          <button
            className="gas-fire"
            onClick={() => Fire(!Fired)}
            aria-label={Fired ? "Stop the preview simulation" : "Fire the domain in the preview"}
          >
            <Glyph Name={Fired ? "CircleCheck" : "MotionActivity"} Size={13} />
            {Fired ? "Stop preview" : "Fire in preview"}
          </button>
        )}
      </Card>

      <Card Title="Budget" Class={Amber ? "gas-amber" : ""}>
        <Metric
          Value={SpellBytes(Resolved.Bytes)}
          Unit=""
          Caption={`${Resolved.Tier.Name} tier · ${Resolved.Tier.Extent ? Resolved.Tier.Extent + "³ lattice" : "no solver"} · ${Resolved.Tier.Sweeps || 0} sweeps · ${Resolved.Hertz || 0} Hz`}
        />
        <Chooser
          Label="Quality"
          Choices={Pin}
          Current={Values.QualityPin || "auto"}
          Change={(Id) => Change("QualityPin", Id)}
        />
        <Control
          Field={{ Label: "Viewer Distance", Control: "Slider", Minimum: 0, Maximum: 200, Decimals: 0, Unit: "m", Default: 12 }}
          Value={Values.Distance ?? 12}
          Change={(Next) => Change("Distance", Next)}
        />
        <p className="gas-note">
          {Values.QualityPin && Values.QualityPin !== "auto"
            ? "Pinned. A pin that outlives the shot it was set for is how a level runs out of budget."
            : `Automatic: ${TierForDistance(Values.Distance ?? 12).Name} at this distance. The ladder is Engine/VolumetricDynamics/GasQualityAllowance.h, and the ceiling is 256 MB over twelve live domains.`}
        </p>
      </Card>

      <Card Title="Source">
        {Resolved.LiveEmitters > 0 ? (
          <p className="gas-note">
            Emission comes from {Resolved.LiveEmitters} child emitter
            {Resolved.LiveEmitters === 1 ? "" : "s"} below. Select one to adjust it.
            {Resolved.LiveEmitters > 1 &&
              " The solver carries one continuous source, so the first enabled emitter drives it; the rest are authored and exported, awaiting the native multi-emitter path."}
          </p>
        ) : (
          F("Emission Rate", "Smoke", "Temperature", "Buoyancy")
        )}
      </Card>

      <Card Title="Appearance">{F("Density", "Albedo", "Fire", "Exposure")}</Card>

      {/* ② Children. Emitters only — see the header of GasSpecification.js for why a collider is not here. */}
      <section className="generic-card gas-children" data-card="Hierarchy">
        <h4>HIERARCHY</h4>
        {Children.length === 0 ? (
          <p className="gas-note">
            No child emitters. The domain emits from its own centre using the Source readings above; add an
            emitter when the flame belongs somewhere other than the middle of the box, or when there is more
            than one of it.
          </p>
        ) : (
          <ul className="gas-child-list">
            {Children.map((Child) => (
              <li key={Child.Id}>
                <button onClick={() => SelectChild(Child.Id)} aria-label={"Select " + Child.Name}>
                  <Icon Name="editor-particle-emitter" Size={16} />
                  <span>{Child.Name}</span>
                  <small>{GasEmitterRowSummary(Child.Values || {})}</small>
                </button>
              </li>
            ))}
          </ul>
        )}
        <button className="gas-add-child" onClick={AddEmitter} aria-label="Add emitter">
          <Glyph Name="PlusAdd" Size={13} />
          Add emitter
        </button>
        <p className="gas-note">
          Colliders are not children. An object obstructs gas by opting in from its own inspector, so it can
          obstruct several domains, move between them, and survive the deletion of any of them.
        </p>
      </section>

      {Departures.length > 0 && (
        <button className="gas-revert" onClick={Revert} aria-label="Revert to preset">
          Revert {Departures.length} change{Departures.length === 1 ? "" : "s"} to {Resolved.PresetName}
        </button>
      )}

      <button className="gas-open" onClick={Open} aria-label="Expand FluidEditor">
        <Glyph Name="ExpandDiagonal" Size={14} />
        Open FluidEditor
        <small>all 85 settings · presets · flipbook bake</small>
      </button>
    </>
  );
}

// ─── A child emitter ───────────────────────────────────────────────────────────────────────────────────────

// 🔴 An emitter is an entity, not a settings group. It has a transform of its own, it can be re-parented
//    onto a tyre or a fracture piece, and it has siblings. That is the entire justification for its being a
//    row rather than a fourth card on the domain — and if it could not be moved independently it would not
//    have earned one.
export function GasEmitterInspector({ Values, Change, Parent, SelectParent, Open }) {
  const F = Reader(GasEmitterSheet, Values, Change);
  return (
    <>
      <div className="tiles">
        <Tile
          Label="Enabled"
          Context="gas"
          On={Values.Enabled !== false}
          Action={() => Change("Enabled", Values.Enabled === false)}
        />
      </div>

      <TransformPanel
        Values={Values}
        Change={Change}
        Rows={GasEmitterTransformRows}
        Space="DOMAIN SPACE"
      />

      <Card Title="Emission">{F("Radius", "Emission Rate", "Smoke", "Temperature", "Fuel", "Rise Speed", "Swirl")}</Card>

      <section className="generic-card gas-children" data-card="Domain">
        <h4>DOMAIN</h4>
        {Parent ? (
          <>
            <ul className="gas-child-list">
              <li>
                <button onClick={() => SelectParent(Parent.Id)} aria-label={"Select " + Parent.Name}>
                  <Icon Name="local-fog" Size={16} />
                  <span>{Parent.Name}</span>
                  <small>{GasRowSummary(Parent.Values || {})}</small>
                </button>
              </li>
            </ul>
            <p className="gas-note">
              Position is in the domain's space, so moving or rotating the domain carries this with it. Drag
              this row onto another object in the outliner to attach the flame to that object instead; the
              domain it emits into is then whichever one encloses it.
            </p>
          </>
        ) : (
          <p className="gas-note">
            Not inside a gas domain. An emitter with no domain is authored but inert — nothing simulates it
            until it is parented to a domain or moved inside one.
          </p>
        )}
      </section>

      {/* ③ The emitter's ↗, which is the particle editor rather than the fluid one. A domain is a volume a
          solver integrates and opens in FluidEditor; an emitter is a source of discrete things -- sparks,
          embers, debris -- and that is what ParticleEditor authors. One card, one editor, chosen by what
          the row actually is. */}
      {Open && (
        <button className="gas-open" onClick={Open} aria-label="Expand ParticleEditor">
          <Glyph Name="ExpandDiagonal" Size={14} />
          Open ParticleEditor
          <small>52 presets · sparks · debris · fibres · lightning</small>
        </button>
      )}
    </>
  );
}

// ─── The full editor ───────────────────────────────────────────────────────────────────────────────────────

// Where the Fluid simulator page is served from. It is the same application as Experimental/Fluid — this
//    drawer frames it rather than reimplementing it, which is the only way the two stay the same UI.
export function FluidEditorSource() {
  // The built simulator, because an iframe cannot reach a dev server on another port when the browser is
  //    not on the same machine as the sandbox. `npm run build` in Experimental/Fluid produces it; pointing
  //    window.FrontierFluidEditorUrl at a running `npm run dev` is the other way, and is the one to use
  //    while working on the simulator itself.
  return (
    (typeof window !== "undefined" && window.FrontierFluidEditorUrl) ||
    "../Fluid/dist/index.html"
  );
}

export default function GasEditor({ Subject, Values, Change, Domains, SelectDomain, Rename, Close, Hidden }) {
  const Resolved = ResolveGas(Values),
    Root = useRef(null),
    Frame = useRef(null),
    [Reached, Reach] = useState(false);

  useEffect(() => {
    const Before = document.activeElement;
    Root.current?.querySelector("button")?.focus();
    return () => Before?.isConnected && Before.focus();
  }, []);

  // 📝 THE HANDOFF, AND IT IS ONE SENTENCE IN EACH DIRECTION.
  //    The drawer posts the resolved scene in; the page posts a changed scene back. Nothing else crosses —
  //    no shared store, no imported module — so the simulator can keep being an independent application and
  //    this editor keeps working when it is not being served.
  useEffect(() => {
    const Receive = (Event) => {
      if (Event.data?.Frontier !== "gas-scene-changed" || !Event.data.Settings) return;
      // The page speaks the simulator's parameter names; the card stores the editor's labels. The sheet is
      //    the dictionary between them, and a setting the card does not expose simply has no label to
      //    land in — it stays in the full editor, which is where it was being adjusted.
      const Arrived = Event.data.Settings;
      for (const Field of GasSheet)
        if (Arrived[Field.Key] !== undefined) Change(Field.Label, Arrived[Field.Key]);
      if (Arrived.boundsWidth !== undefined || Arrived.boundsHeight !== undefined)
        Change("Bounds", [
          Arrived.boundsWidth ?? Resolved.Bounds[0],
          Arrived.boundsHeight ?? Resolved.Bounds[1],
          Arrived.boundsWidth ?? Resolved.Bounds[2],
        ]);
    };
    window.addEventListener("message", Receive);
    return () => window.removeEventListener("message", Receive);
  }, [Values, Change]);

  const Hand = () => {
    Reach(true);
    Frame.current?.contentWindow?.postMessage(
      { Frontier: "gas-scene", Name: Subject.Name, Preset: Resolved.Preset, Settings: Resolved.Settings },
      "*",
    );
  };

  const Key = (Event) => {
    if (Event.key === "Escape") {
      Event.stopPropagation();
      Close();
    }
  };

  return (
    <div className="gas-editor-backdrop">
      <section
        className="gas-editor"
        role="dialog"
        aria-modal="true"
        aria-label="FluidEditor"
        ref={Root}
        onKeyDown={Key}
      >
        <header className="gas-editor-header">
          <div>
            <span className="eyebrow">VOLUMETRICS / FULL SIMULATION</span>
            <h1>FluidEditor</h1>
          </div>
          <select
            aria-label="Edited gas domain"
            value={Subject.Id}
            onChange={(Event) => SelectDomain(Event.target.value)}
          >
            {(Domains || []).map((Item) => (
              <option key={Item.Id} value={Item.Id}>
                {Item.Name}
              </option>
            ))}
          </select>
          <label className="gas-name">
            Name
            <input
              aria-label="Gas domain name"
              value={Subject.Name}
              maxLength={80}
              onChange={(Event) => Rename(Event.target.value)}
            />
          </label>
          <button className="gas-close" aria-label="Close FluidEditor" onClick={Close}>
            ×
          </button>
        </header>

        <div className="gas-editor-body">
          <iframe
            ref={Frame}
            className="gas-editor-frame"
            title="Fluid simulator"
            src={FluidEditorSource()}
            onLoad={Hand}
          />
          {!Reached && (
            <div className="gas-editor-absent">
              <h3>The Fluid simulator page is not being served</h3>
              <p>
                This drawer hosts <code>Experimental/Fluid</code> rather than reimplementing it, so the full
                editor here and the standalone one are the same UI by construction. Build it with
                <code> cd Experimental/Fluid &amp;&amp; npm run build</code>, or set
                <code> window.FrontierFluidEditorUrl</code> to a running <code>npm run dev</code>.
              </p>
              <p>
                It carries the preset rail and its four filters, the grouped inspector over all 85 settings,
                the viewport with bounds box and lattice lines, the debug channel selector, and the flipbook
                bake panel. The eleven settings on the card are a subset of that page, not a parallel copy.
              </p>
            </div>
          )}
        </div>

        <footer>
          <span>
            <i className={Hidden ? "red" : "green"} />
            {Hidden ? "Domain hidden · nothing simulates" : GasSummary(Resolved)}
          </span>
          <span>Saved with the scene · crosses to the engine as .gasscene.toml</span>
        </footer>
      </section>
    </div>
  );
}

// ─── The particle editor ───────────────────────────────────────────────────────────────────────────────────

// Where the Particle Editor page is served from. Unlike the Fluid simulator it has no build step at all —
//    three script tags over a window global — so the source file itself is the page, and there is no dist
//    to point at. That is convenient here and is the first thing to fix about it; see Docs/ParticleEditor.md.
export function ParticleEditorSource() {
  return (
    (typeof window !== "undefined" && window.FrontierParticleEditorUrl) ||
    "../ParticleEditor/index.html"
  );
}

// 🔴 THE EMITTER'S EXPANDED EDITOR, AND WHY IT IS A DIFFERENT PAGE FROM THE DOMAIN'S.
//    A gas domain is a volume that a solver integrates: its expanded editor is the Fluid simulator, which
//    is that same solver with a viewport attached. An emitter is a source of discrete things, and what an
//    author wants to open for one is a particle system — sparks off a grinder, embers off a fire, debris
//    off a fracture. Opening FluidEditor for it would show the domain's settings again under a different
//    title, which is how an editor ends up with two of everything.
export function ParticleEditor({ Subject, Values, Change, Rename, Close }) {
  const Root = useRef(null),
    Frame = useRef(null),
    [Reached, Reach] = useState(false);

  useEffect(() => {
    const Before = document.activeElement;
    Root.current?.querySelector("button")?.focus();
    return () => Before?.isConnected && Before.focus();
  }, []);

  // The handoff, in the same two sentences the Fluid drawer uses. Only the four readings in
  //    GasEmitterToParticles cross; everything else on either side stays where it was authored.
  useEffect(() => {
    const Receive = (Event) => {
      if (Event.data?.Frontier !== "particle-system-changed" || !Event.data.Settings) return;
      const Arrived = EmitterValuesFromParticles(Event.data.Settings);
      for (const [Label, Reading] of Object.entries(Arrived)) Change(Label, Reading);
    };
    window.addEventListener("message", Receive);
    return () => window.removeEventListener("message", Receive);
  }, [Change]);

  const Hand = () => {
    Reach(true);
    Frame.current?.contentWindow?.postMessage(
      {
        Frontier: "particle-system",
        Name: Subject.Name,
        Preset: Values.ParticlePreset || "embers",
        Settings: ParticleSettingsFromEmitter(Values),
      },
      "*",
    );
  };

  const Key = (Event) => {
    if (Event.key === "Escape") {
      Event.stopPropagation();
      Close();
    }
  };

  return (
    <div className="gas-editor-backdrop">
      <section
        className="gas-editor"
        role="dialog"
        aria-modal="true"
        aria-label="ParticleEditor"
        ref={Root}
        onKeyDown={Key}
      >
        <header className="gas-editor-header">
          <div>
            <span className="eyebrow">VOLUMETRICS / EMITTER</span>
            <h1>ParticleEditor</h1>
          </div>
          <label className="gas-name">
            Name
            <input
              aria-label="Emitter name"
              value={Subject.Name}
              maxLength={80}
              onChange={(Event) => Rename(Event.target.value)}
            />
          </label>
          <button className="gas-close" aria-label="Close ParticleEditor" onClick={Close}>
            ×
          </button>
        </header>

        <div className="gas-editor-body">
          <iframe
            ref={Frame}
            className="gas-editor-frame"
            title="Particle editor"
            src={ParticleEditorSource()}
            onLoad={Hand}
          />
          {!Reached && (
            <div className="gas-editor-absent">
              <h3>The Particle Editor page is not being served</h3>
              <p>
                This drawer hosts <code>Experimental/ParticleEditor</code> rather than reimplementing it,
                exactly as the domain's drawer hosts <code>Experimental/Fluid</code>. Serve the repository
                root, or set <code>window.FrontierParticleEditorUrl</code>.
              </p>
              <p>
                It needs WebGPU: it has no WebGL2 path, so a browser without WebGPU shows its own boot
                banner rather than a viewport.
              </p>
            </div>
          )}
        </div>

        <footer>
          <span>
            <i className="green" />
            {GasEmitterToParticles.length} readings cross · the rest stay in their own editor
          </span>
          <span>Saved with the scene · the emitter is a row, not a settings group</span>
        </footer>
      </section>
    </div>
  );
}
