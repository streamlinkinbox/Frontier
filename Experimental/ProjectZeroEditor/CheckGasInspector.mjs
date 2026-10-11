//============================================================================================================================================
//                                                        CHECKGASINSPECTOR.MJS
//============================================================================================================================================
// 📦 The gas card's arithmetic, its wiring, and its presence in the shipped bundle — without a browser.
//
//    node Experimental/ProjectZeroEditor/CheckGasInspector.mjs
//
// 💡 WHY THIS ONE DOES NOT USE PLAYWRIGHT, WHEN CheckWind.mjs DOES.
//    The wind check drives a real page because what it is checking is a drag interaction on an SVG. What is
//    worth checking here is arithmetic that must agree with a C++ header, and whether the card survived the
//    build — neither needs a browser, and a check that needs one does not run on a machine without one.
//    A playwright pass over the rendered card belongs beside CheckWind.mjs when the card stops moving.
//
// 🔴 THE COST READOUT IS CHECKED AGAINST GasQualityAllowance.h BY READING IT.
//    A number copied from a header into a panel is correct on the day it is copied. These are compared
//    against the header itself, so the panel cannot keep showing 94 MB after the allowance changes.

import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  ApplyGasPreset,
  GasBytesPerVoxel,
  GasEmitterSheet,
  GasEmitterTransformRows,
  GasPresetChoices,
  GasRowSummary,
  GasRunPolicies,
  GasRunning,
  GasSummary,
  GasSheet,
  GasTiers,
  GasTransformRows,
  NewGasDomain,
  NewGasEmitter,
  ResolveGas,
  SpellBytes,
  TierBytes,
  TierForDistance,
} from "./GasSpecification.js";

const Folder = path.dirname(fileURLToPath(import.meta.url));
const Root = path.resolve(Folder, "../..");
const Read = (Relative) => fs.readFileSync(path.join(Root, Relative), "utf8");

let Checks = 0;
const Check = (Condition, Claim) => {
  Checks++;
  assert.ok(Condition, Claim);
  console.log("  PASS  " + Claim);
};
const Banner = (Title) => console.log("\n" + Title);

console.log("CheckGasInspector — the gas card");

// ─── The ladder, against the header it mirrors ─────────────────────────────────────────────────────────────

Banner("The quality ladder agrees with GasQualityAllowance.h");
{
  const Header = Read("Engine/VolumetricDynamics/GasQualityAllowance.h");

  const Bytes = Number(/GasBytesPerVoxel\s*=\s*(\d+)u/.exec(Header)?.[1]);
  Check(Bytes === GasBytesPerVoxel, `bytes a voxel is ${Bytes} in both the header and the panel`);

  // Each rung's extent and sweeps, read out of the allowance table rather than trusted.
  const Table = /AllowanceFor[\s\S]*?\n}/.exec(Header)?.[0] || "";
  for (const Tier of GasTiers) {
    const Name = Tier.Name;
    const Row = new RegExp(`GasQuality::${Name}:[^;]*?return\\s*\\{\\s*(\\d+)u?\\s*,\\s*(\\d+)u?`, "i").exec(
      Table,
    );
    Check(Boolean(Row), `the header declares an allowance for ${Name}`);
    Check(
      Number(Row[1]) === Tier.Extent,
      `${Name} is ${Tier.Extent}³ in both — a panel showing a lattice the solver will not allocate is decoration`,
    );
    Check(Number(Row[2]) === Tier.Sweeps, `and ${Tier.Sweeps} pressure sweeps in both`);
  }

  // The distance bands, which decide which rung a domain is granted.
  const Bands = /QualityForDistance[\s\S]*?\n}/.exec(Header)?.[0] || "";
  for (const Tier of GasTiers.filter((Entry) => Number.isFinite(Entry.Within)))
    Check(
      Bands.includes(`${Tier.Within}.0f`),
      `the ${Tier.Name} band ends at ${Tier.Within} m in the header as it does in the panel`,
    );

  Check(
    TierForDistance(5).Id === "hero" &&
      TierForDistance(20).Id === "near" &&
      TierForDistance(50).Id === "mid" &&
      TierForDistance(120).Id === "far" &&
      TierForDistance(400).Id === "flipbook",
    "and the panel grants the same rung at 5, 20, 50, 120 and 400 metres",
  );
  Check(TierBytes(GasTiers[4]) === 0, "a flipbook costs no live storage, which is the point of the rung");
  Check(SpellBytes(TierBytes(GasTiers[0])) === "94 MB", "a Hero domain reads as 94 MB");
}

// ─── The run policy ────────────────────────────────────────────────────────────────────────────────────────

Banner("A domain is in the scene whether or not it simulates");
{
  const Dormant = ResolveGas({ ...NewGasDomain(), Run: "dormant" });
  Check(!GasRunning(Dormant, false), "a dormant domain does not simulate");
  Check(GasRunning(Dormant, true), "and does once something fires it");
  Check(
    GasSummary(Dormant).includes("not simulating"),
    "the card says so in words rather than only in a colour",
  );

  const Always = ResolveGas({ ...NewGasDomain(), Run: "always" });
  Check(GasRunning(Always, false), "an always-on domain simulates with nothing firing it");

  const Distant = ResolveGas({ ...NewGasDomain(), Run: "proximity", Distance: 400 });
  Check(
    !GasRunning(Distant, true),
    "a domain past the ladder does not simulate even when fired — beyond 150 m it is a flipbook, and firing a card does nothing",
  );

  const Near = ResolveGas({ ...NewGasDomain(), Run: "proximity", Distance: 40 });
  Check(GasRunning(Near, false), "and inside the ladder proximity alone is enough");

  Check(GasRunPolicies[0].Id === "dormant", "dormant is first, because in a level it is the normal case");
  Check(
    NewGasDomain("camp_fire").Run === "always" && NewGasDomain("ue5_pyro_default").Run === "triggered",
    "a new domain defaults to the policy its preset implies: a fire burns, a detonation waits",
  );
  Check(
    NewGasDomain().Coupled === false,
    "🔴 two-way coupling is off on a new domain, as GasWindContribution.h has it",
  );
}

// ─── Resolution ────────────────────────────────────────────────────────────────────────────────────────────

Banner("Defaults, then preset, then this domain's own differences");
{
  Check(GasPresetChoices.length === 18, "all eighteen presets are offered, read from presets.js rather than listed again");
  Check(
    GasPresetChoices.filter((Entry) => Entry.OneShot).length > 0,
    "and the one-shots are marked, because the policy a user should pick depends on it",
  );

  const Plain = ResolveGas(ApplyGasPreset("camp_fire", {}));
  const Changed = ResolveGas({ ...ApplyGasPreset("camp_fire", {}), Bounds: [7.5, 2.1, 7.5] });
  Check(Changed.Settings.boundsWidth === 7.5, "the transform's Bounds row is what the solver reads as boundsWidth");
  Check(
    Changed.Settings.buoyancy === Plain.Settings.buoyancy,
    "and changes nothing else, so a domain cannot freeze a preset reading it never touched",
  );
  Check(
    ResolveGas({}).Settings.gridResolution > 0,
    "a domain with no stored settings at all still resolves rather than throwing",
  );

  for (const Field of GasSheet)
    Check(
      Plain.Settings[Field.Key] !== undefined,
      `the inline setting ${Field.Key} exists in the simulator's own parameters`,
    );
  Check(
    GasSheet.length === 11,
    "eleven settings are inline; the other seventy-four are one button away",
  );
  Check(
    !GasSheet.some((Field) => Field.Key === "gridResolution"),
    "⚠️ the lattice is NOT a slider — the quality ladder owns it, and a control the budget overrules is a lie with a handle on it",
  );
  Check(
    GasSheet.every((Field) => Field.Minimum === undefined || Field.Maximum > Field.Minimum),
    "every slider has a range the editor's Control can clamp against",
  );

  Check(
    GasRowSummary({ ...ApplyGasPreset("camp_fire", {}), Run: "always", Distance: 4 }).includes("Hero"),
    "the outliner row carries extent, tier and policy in one line, as the wind and fog rows do",
  );
}

// ─── The transform, and the hierarchy ──────────────────────────────────────────────────────────────────────

Banner("A gas domain is a 3D entity, so it has a transform and children");
{
  const Rows = GasTransformRows.map((Row) => Row[0]);
  Check(Rows[0] === "Position" && Rows[1] === "Rotation", "position and rotation are the first two rows, as on every other entity");
  Check(
    Rows[2] === "Bounds" && !Rows.includes("Scale"),
    "① bounds take the row Scale occupies on a mesh — the domain's extent IS its scale, in metres, and is not a child",
  );
  Check(
    GasTransformRows[2][1] === "m",
    "and they are in metres rather than a multiplier, because a solver cube has a real size",
  );

  const Fresh = NewGasDomain("camp_fire");
  Check(Array.isArray(Fresh.Position) && Array.isArray(Fresh.Rotation), "a new domain starts with a transform, not with one implied later");
  const Resolved = ResolveGas(Fresh);
  Check(
    Resolved.Settings.boundsWidth === Resolved.Bounds[0] && Resolved.Settings.boundsHeight === Resolved.Bounds[1],
    "the bounds row and the solver's bounds are one reading, not two that have to be kept level",
  );
  Check(
    Resolved.Bounds[0] === Resolved.Bounds[2],
    "X and Z stay equal: the browser solver uses one width for both horizontal axes, so a second number would be a number to get wrong",
  );

  // ② Emitters are children; ③ colliders are not.
  const Emitter = NewGasEmitter();
  Check(Array.isArray(Emitter.Position), "② an emitter has a transform of its own, which is what makes it an entity rather than a settings group");
  Check(
    GasEmitterTransformRows.length === 2 && !GasEmitterTransformRows.some((Row) => Row[0] === "Bounds"),
    "and no bounds of its own — the box belongs to the domain",
  );
  const One = ResolveGas(Fresh, [{ Id: "a", Values: { ...Emitter, Temperature: 9 } }]);
  Check(One.Settings.emitterTemperature === 9, "the first enabled emitter drives the solver's single continuous source");
  const Two = ResolveGas(Fresh, [
    { Id: "a", Values: { ...Emitter, Enabled: false } },
    { Id: "b", Values: { ...Emitter, Temperature: 4 } },
  ]);
  Check(Two.LiveEmitters === 1 && Two.Settings.emitterTemperature === 4, "a disabled sibling is skipped rather than silently driving it");
  Check(
    ResolveGas(Fresh, [{ Id: "a", Values: { ...Emitter, Enabled: false } }]).Settings.emitterEnabled === false,
    "and with every emitter off the domain emits nothing, instead of falling back to its own centre",
  );
  Check(
    ResolveGas(Fresh, [{ Id: "a", Values: Emitter }, { Id: "b", Values: Emitter }]).Carried === 1,
    "🚩 two live emitters are authored and exported, but only one crosses to the solver today — counted, not hidden",
  );
}

// ─── The wiring, and the bundle ────────────────────────────────────────────────────────────────────────────

Banner("Wired into the editor, drawn by the editor's own controls, and present in what ships");
{
  const Editor = Read("Experimental/ProjectZeroEditor/Editor.jsx");
  const Inspectors = Read("Experimental/ProjectZeroEditor/Inspectors.jsx");
  const Panel = Read("Experimental/ProjectZeroEditor/GasPanel.jsx");
  const Sheet = Read("Experimental/ProjectZeroEditor/GasSpecification.js");
  const Style = Read("Experimental/ProjectZeroEditor/GasPanel.css");

  Check(/\[\s*"gas-domain",/.test(Editor), "a gas domain is an outliner row");
  Check(
    /"gas-domain"[\s\S]{0,160}"showcase"/.test(Editor),
    "and it sits in the scene collection rather than under World — it is an object, not a global field",
  );
  Check(/"gas-emitter",[\s\S]{0,120}"gas-domain"/.test(Editor), "② the emitter ships as a child row of the domain, with the domain as its parent");
  Check(Editor.includes("GasEmitterRowSummary(Record)"), "and carries its own summary in the outliner");
  Check(Editor.includes("AddEmitter"), "a domain can gain another emitter, so siblings are reachable without the construct menu");
  Check(Editor.includes("GasRowSummary(Record)"), "the domain row carries its summary");
  Check(Editor.includes("<GasEditor"), "the full editor is mounted beside WindEditor");
  Check(Editor.includes('aria-label="Expand FluidEditor"'), "and focus returns to the expand button on close");
  Check(Inspectors.includes("<GasInspector"), "the card is rendered for a gas subject");
  Check(Inspectors.includes("<GasEmitterInspector"), "and the emitter has an inspector of its own");
  Check(Inspectors.includes("OpenGas"), "the expand button is wired to the drawer");
  Check(
    !Read("Experimental/ProjectZeroEditor/NativePanels.json").includes('"gas"'),
    "⚠️ no hand-added gas entry in NativePanels.json — that file is generated from CelestialSequence.cpp and would claim a native provenance the gas panel has not got yet",
  );

  // 🔴 The styling complaint, turned into a check: the panel must use the editor's primitives, not its own.
  Check(
    /import \{[^}]*Card[^}]*Control[^}]*\} from "\.\/Inspectors\.jsx"/.test(Panel),
    "🔴 the panel draws through the inspector's own Card and Control rather than restyling them",
  );
  Check(Panel.includes("TransformPanel"), "and through the editor's own TransformPanel, so a gas transform is the transform");
  Check(Panel.includes("<Tile"), "quick controls are the editor's tiles");
  Check(Panel.includes("<Metric"), "and the cost reads out in the editor's metric, like every other card's headline number");
  Check(
    !/type="range"/.test(Panel),
    "no hand-rolled range input survives in the panel — that was what made it read as a visitor from another application",
  );
  Check(!/\.gas-slider|\.gas-row|\.gas-tabs|\.gas-readout/.test(Style), "and the stylesheet no longer carries a second set of rows, pills, tabs and readouts");
  Check(
    Style.includes(".gas-visual") && Style.includes(".gas-state"),
    "what is left is the preview, the run state, the child list and the drawer — the four things the shared vocabulary has no word for",
  );
  Check(
    Sheet.includes('Control: "Slider"') && Sheet.includes("Minimum,") && Sheet.includes("Decimals,"),
    "the gas sheet is written in the same descriptor shape NativePanels.json uses, which is why one Control can draw both",
  );

  Check(
    Panel.includes("DORMANT · NOT SIMULATING"),
    "🔴 the preview refuses to animate a domain the game would not run",
  );
  Check(
    Sheet.includes("OBSTACLES ARE NOT CHILDREN"),
    "③ and the reason a collider is not parented to the domain is written down where the next person will look",
  );
  Check(
    Sheet.includes("ROTATION DOES NOT REACH THE SOLVER") && Panel.includes("ROTATION ORIENTS CHILDREN"),
    "⚠️ the axis-aligned lattice is admitted in the card itself, not just in a comment",
  );
  Check(
    /iframe/.test(Panel) && Panel.includes("Experimental/Fluid"),
    "the full editor hosts the existing simulator rather than reimplementing it — that is what keeps the two the same UI",
  );

  const Bundle = Read("Experimental/ProjectZeroEditor/index.html");
  Check(Bundle.includes("Expand FluidEditor"), "the expand button survived the build into the standalone bundle");
  Check(Bundle.includes("DORMANT"), "and the dormant state");
  Check(Bundle.includes("ROTATION ORIENTS CHILDREN"), "and the rotation caveat");
  Check(Bundle.includes("Add emitter"), "and the hierarchy's one affordance");
  for (const Policy of GasRunPolicies)
    Check(Bundle.includes(Policy.Description.slice(0, 40)), `the ${Policy.Name} policy explains itself in the bundle`);
  for (const Field of GasEmitterSheet)
    Check(Bundle.includes(Field.Label), `the emitter's ${Field.Label} reaches the bundle`);
}

console.log(`\nPASS ${Checks}`);
