//============================================================================================================================================
//                                                           GASSPECIFICATION.JS
//============================================================================================================================================
// 📦 A gas domain as a scene entity: a transform, a hierarchy, the few settings worth having inline, and what it costs.
//
// 🔴 A DOMAIN IS IN THE SCENE WHETHER OR NOT IT IS SIMULATING, AND THOSE ARE DIFFERENT QUESTIONS.
//    An outliner row exists from the moment it is added — it has a transform, bounds, a preset, a name and a
//    place in the hierarchy, and it draws its box in the viewport. Simulating is a separate state that
//    something has to ask for. Forty gas rows in a level is ordinary; forty live solvers is not, and the
//    ceiling is twelve. So every domain carries a Run policy, and the default is Dormant.
//
// ─── THE HIERARCHY, AND WHY IT IS THIS SHAPE AND NOT THE OBVIOUS ONE ────────────────────────────────────────
//
//    Gas Domain          the box: transform, bounds, budget, run policy, look
//    └── Emitter         a child entity with its own transform, in the domain's space
//    └── Emitter 2       siblings; a bonfire with three seats of flame is three of these
//
//    Three decisions in that, and each one is a thing *not* done:
//
//    ① BOUNDS ARE NOT A CHILD. They are the domain's own extent — the transform's scale, in metres. A
//       "Bounds" row would be a child that cannot be moved independently of its parent, cannot be deleted,
//       and cannot be duplicated, which is to say it is not an entity. It is a property wearing a row.
//
//    ② OBSTACLES ARE NOT CHILDREN EITHER, AND THAT ONE IS LOAD BEARING. A crate that blocks smoke is a crate
//       first: it has its own transform, its own mesh, its own place in the hierarchy, and it is obstructing
//       three domains at once as it slides between them. Parenting it to one of them would mean deleting a
//       gas effect deletes the crate. Objects opt in from their own inspector, the domain reads whatever
//       overlaps it, and the opt-in set stays data rather than hierarchy.
//
//    ③ AN EMITTER *IS* A CHILD, AND CAN BE RE-PARENTED AWAY. It is the one part with a position of its own
//       that means nothing without the domain — but attaching one to a tyre or a fracture piece is exactly
//       the case the port exists for, so re-parenting is allowed and the domain is resolved by enclosure
//       rather than by ownership. Parented here by default, because that is where it is authored.
//
// ⚠️ ROTATION DOES NOT REACH THE SOLVER, AND THE CARD SAYS SO.
//    CoarseGasField is an axis-aligned cube and GasCollider carries no orientation — deliberately, and for
//    the reason written in GasCollisionIntake.h. A domain's rotation therefore places and orients its
//    *children* and the authored framing; the lattice stays axis-aligned. Hiding that would produce a plume
//    that ignores a rotation somebody spent a minute setting.
//
// The quality ladder below mirrors Engine/VolumetricDynamics/GasQualityAllowance.h. The numbers are that
//    file's, not new ones, and CheckGasInspector.mjs compares them by reading the header.

import { PRESETS, DEFAULT_PARAMS } from "../Fluid/src/presets.js";

// ─── The run policy ────────────────────────────────────────────────────────────────────────────────────────

export const GasRunPolicies = [
  {
    Id: "dormant",
    Name: "Dormant",
    Short: "never until fired",
    Description:
      "Present in the scene and drawing its bounds, simulating nothing until a gameplay event fires it. A fracture burst or a detonation is this.",
  },
  {
    Id: "triggered",
    Name: "On trigger",
    Short: "fires, runs, retires",
    Description:
      "Dormant until triggered, then simulates for its lifetime and retires. Most gas is this: a one-shot that runs once, is destroyed, and gives its fields back.",
  },
  {
    Id: "proximity",
    Name: "On proximity",
    Short: "within range",
    Description:
      "Simulates while the viewer is inside the far band and sleeps beyond it. Proximity decides whether it runs; the budget still decides how well.",
  },
  {
    Id: "always",
    Name: "Always",
    Short: "persistent",
    Description:
      "A camp fire, a chimney, a vent. Runs whenever the level is loaded, at whatever tier the budget grants.",
  },
];

// ─── Retirement ────────────────────────────────────────────────────────────────────────────────────────────

// 🔴 MOST GAS IS A ONE-SHOT, AND A ONE-SHOT THAT STAYS RESIDENT IS A LEAK WITH A SCHEDULE.
//    A detonation, a dust hit, a pipe burst: fires once, runs for a few seconds, and is finished. Finished
//    has to mean destroyed — the row may stay in the scene, but the solver's fields are handed back the
//    moment the last visible wisp has gone, or forty one-shots in a corridor spend the whole level holding
//    forty times 1.4 MB of velocity and smoke that nothing is reading.
//
//    Three phases, and the middle one exists because the alternative looks broken:
//
//      Running    the solver advances; the domain holds its fields.
//      Fading     emission has stopped, the solver still advances, the plume dissipates on its own.
//                 Cutting at the lifetime instead would make smoke vanish mid-air.
//      Released   nothing advances and the fields are gone. The row remains, dormant and re-firable; what
//                 was released is the storage, not the entity.
//
//    The fade is charged to the budget like any other running domain, because it is one.
export const GasFadeSeconds = 1.6;

export function GasRetires(Resolved) {
  return Resolved.Policy.Id === "triggered" && Resolved.Retire !== false;
}

// What a one-shot occupies over its whole life, and for how long — the number that decides whether twelve
//    domains is the ceiling or whether the corridor can have forty.
export function GasResidency(Resolved) {
  const Running = Number(Resolved.Lifetime || 0) + GasFadeSeconds;
  return {
    Seconds: GasRetires(Resolved) ? Running : Infinity,
    Held: Resolved.Bytes,
    Released: GasRetires(Resolved) ? Resolved.Bytes : 0,
  };
}

// ─── The quality ladder, from GasQualityAllowance.h ────────────────────────────────────────────────────────

export const GasBytesPerVoxel = 47;
export const GasMasterHertz = 60;

export const GasTiers = [
  { Id: "hero", Name: "Hero", Extent: 128, Sweeps: 24, StepDivision: 1, Within: 8 },
  { Id: "near", Name: "Near", Extent: 96, Sweeps: 18, StepDivision: 1, Within: 25 },
  { Id: "mid", Name: "Mid", Extent: 64, Sweeps: 12, StepDivision: 2, Within: 60 },
  { Id: "far", Name: "Far", Extent: 32, Sweeps: 8, StepDivision: 4, Within: 150 },
  { Id: "flipbook", Name: "Flipbook", Extent: 0, Sweeps: 0, StepDivision: 0, Within: Infinity },
];

export const GasCeiling = { Bytes: 256 * 1024 * 1024, DomainLimit: 12 };

export const TierForDistance = (Distance) =>
  GasTiers.find((Tier) => Distance < Tier.Within) || GasTiers[4];

export const TierById = (Id) => GasTiers.find((Tier) => Tier.Id === Id) || GasTiers[2];

// Live storage for one domain. A flipbook costs nothing live — it is a texture shared by every domain using
//    the same preset, which is the whole reason the rung exists.
export const TierBytes = (Tier) => (Tier.Extent ? Math.pow(Tier.Extent, 3) * GasBytesPerVoxel : 0);

export function SpellBytes(Bytes) {
  if (!Bytes) return "0 MB";
  const Mega = Bytes / (1024 * 1024);
  return Mega >= 10 ? `${Math.round(Mega)} MB` : `${Mega.toFixed(1)} MB`;
}

// The solver rate is not the frame rate: a Mid domain steps every second master tick, a Far one every fourth.
export const TierHertz = (Tier) => (Tier.StepDivision ? GasMasterHertz / Tier.StepDivision : 0);

// ─── The sheets ────────────────────────────────────────────────────────────────────────────────────────────

// 📝 Written in the shape Panels[...] uses, so the gas cards are drawn by the same Control the fog and sun
//    cards are drawn by. A panel that styles its own sliders is a panel that stops matching the editor the
//    first time the editor changes. `Key` is the simulator's own parameter name, which is what makes a
//    reading here and a reading in the Fluid Editor the same reading rather than two that resemble each
//    other. Labels follow the editor's Title Case; keys stay camelCase because presets.js owns them.
const Slider = (Label, Group, Key, Minimum, Maximum, Decimals, Unit) => ({
  Label,
  Group,
  Key,
  Control: "Slider",
  Minimum,
  Maximum,
  Decimals,
  Unit,
  Default: DEFAULT_PARAMS[Key],
});

const Switch = (Label, Group, Key) => ({
  Label,
  Group,
  Key,
  Control: "Switch",
  Default: DEFAULT_PARAMS[Key],
});

export const GasSheet = [
  // Domain — what the box is, before anything is in it. The lattice is deliberately absent: the quality
  //    ladder owns the extent at runtime, and a slider that the budget overrules every frame is a lie with
  //    a handle on it. The Budget card reads the granted lattice out instead.
  Switch("Dynamic Bounds", "Domain", "dynamicBounds"),
  Slider("Surge Limit", "Domain", "dynamicBoundsMax", 1, 3, 2, "×"),
  Switch("Enclosed Box", "Domain", "enclosedBox"),

  // Source — what comes out, and how hard.
  Slider("Emission Rate", "Source", "emitterRate", 0, 3, 2, "×"),
  Slider("Smoke", "Source", "emitterSmoke", 0, 8, 2, ""),
  Slider("Temperature", "Source", "emitterTemperature", 0, 12, 2, "K"),
  Slider("Buoyancy", "Source", "buoyancy", -4, 16, 2, "m/s²"),
  // Rise speed belongs to an emitter, and turbulence to the full editor. Eleven readings is the number that
  //    fits a 320-pixel column without scrolling past the expand button.

  // Appearance — the lighting vocabulary the raymarch uses, under the names it uses.
  Slider("Density", "Appearance", "densityExtinction", 0, 60, 1, "1/m"),
  Slider("Albedo", "Appearance", "smokeAlbedo", 0, 1, 2, ""),
  Slider("Fire", "Appearance", "fireIntensity", 0, 20, 2, "×"),
  Slider("Exposure", "Appearance", "exposure", 0.1, 4, 2, "×"),
];

export const GasEmitterSheet = [
  Slider("Radius", "Emission", "emitterRadius", 0.01, 1, 3, ""),
  Slider("Emission Rate", "Emission", "emitterRate", 0, 3, 2, "×"),
  Slider("Smoke", "Emission", "emitterSmoke", 0, 8, 2, ""),
  Slider("Temperature", "Emission", "emitterTemperature", 0, 12, 2, "K"),
  Slider("Fuel", "Emission", "emitterFuel", 0, 12, 2, ""),
  Slider("Rise Speed", "Emission", "emitterUpwardVelocity", 0, 12, 2, "m/s"),
  Slider("Swirl", "Emission", "emitterSwirl", 0, 6, 2, ""),
];

// Bounds are the transform's third row rather than two more sliders — see ① in the header. X and Z are kept
//    equal because the authored domain is a square prism, and a width that is two numbers is two numbers to
//    get wrong.
export const GasTransformRows = [
  ["Position", "m", [0, 0, 0], -100000, 100000, 0.01],
  ["Rotation", "deg", [0, 0, 0], -36000, 36000, 0.1],
  ["Bounds", "m", [1.85, 2.1, 1.85], 0.1, 64, 0.01],
];

// ─── The emitter's crossing to the Particle Editor ─────────────────────────────────────────────────────────

// 🔴 A gas emitter and a particle system are not the same object and most of their settings do not
//    correspond at all: smoke, fuel and temperature are fields a fluid solver integrates, and a particle
//    system has no fields. Four readings do correspond, exactly, and those are the four that cross. The
//    rest stay where they were authored. Inventing a conversion for the others would be worse than not
//    having one -- it would look like it worked.
export const GasEmitterToParticles = [
  ["Position",   "origin",    "m"],
  ["Radius",     "radius",    "m"],
  ["Rise Speed", "buoyancy",  "m/s"],
  ["Swirl",      "swirl",     ""],
];

// 📦 The particle settings a gas emitter's readings imply.
export function ParticleSettingsFromEmitter(Values) {
  const Settings = {};
  for (const [Label, Key] of GasEmitterToParticles) {
    const Reading = Values?.[Label];
    if (Reading === undefined || Reading === null) continue;
    if (Array.isArray(Reading)) {
      if (Reading.length === 3 && Reading.every((One) => Number.isFinite(One))) Settings[Key] = Reading.slice();
    } else if (Number.isFinite(Reading)) Settings[Key] = Reading;
  }
  return Settings;
}

// 📦 The emitter readings a returning particle system implies. The inverse of the above over the same four.
export function EmitterValuesFromParticles(Settings) {
  const Values = {};
  for (const [Label, Key] of GasEmitterToParticles) {
    const Reading = Settings?.[Key];
    if (Reading === undefined || Reading === null) continue;
    if (Array.isArray(Reading)) {
      if (Reading.length === 3 && Reading.every((One) => Number.isFinite(One))) Values[Label] = Reading.slice();
    } else if (Number.isFinite(Reading)) Values[Label] = Reading;
  }
  return Values;
}

export const GasEmitterTransformRows = [
  ["Position", "m", [0, 0, 0], -1000, 1000, 0.01],
  ["Rotation", "deg", [0, 0, 0], -36000, 36000, 0.1],
];

// ─── Presets ───────────────────────────────────────────────────────────────────────────────────────────────

export const GasPresetChoices = Object.entries(PRESETS).map(([Id, Entry]) => ({
  Id,
  Name: Entry.name,
  Description: Entry.description,
  OneShot: Boolean(Entry.triggerExplosionOnLoad),
}));

export const GasPresetOptions = GasPresetChoices.map(
  (Entry) => Entry.Name + (Entry.OneShot ? " · one-shot" : ""),
);

// Applying a preset writes the sheet's readings and the bounds, and nothing else — a transform is where the
//    author put the effect and is not the preset's business.
export function ApplyGasPreset(Id, Values = {}) {
  const Preset = PRESETS[Id] || PRESETS.camp_fire;
  const Settings = { ...DEFAULT_PARAMS, ...(Preset.params || {}) };
  const Next = { ...Values, Preset: Id, Run: Preset.triggerExplosionOnLoad ? "triggered" : "always" };
  for (const Field of GasSheet) Next[Field.Label] = Settings[Field.Key];
  Next.Bounds = [Settings.boundsWidth, Settings.boundsHeight, Settings.boundsWidth];
  return Next;
}

export function NewGasDomain(Preset = "camp_fire") {
  return ApplyGasPreset(Preset, {
    Position: [0, 0, 0],
    Rotation: [0, 0, 0],
    QualityPin: "auto",
    Distance: 12,
    Lifetime: 4,
    // 🔴 A one-shot gives its fields back by default. Holding them is the choice that has to be made
    //    deliberately, because it is the choice that costs something all level.
    Retire: true,
    Obstructs: true,
    // 🔴 Two-way coupling off on a new domain, as GasWindContribution.h has it. A default-on coupling is a
    //    vehicle being shoved by its own exhaust.
    Coupled: false,
  });
}

export function NewGasEmitter() {
  const Settings = { ...DEFAULT_PARAMS, ...(PRESETS.camp_fire.params || {}) };
  const Next = { Position: [0, 0, 0], Rotation: [0, 0, 0], Enabled: true, AttachedTo: "" };
  for (const Field of GasEmitterSheet) Next[Field.Label] = Settings[Field.Key];
  return Next;
}

// ─── Resolution ────────────────────────────────────────────────────────────────────────────────────────────

// Flat labelled readings in, one resolved domain out. The settings object uses the simulator's parameter
//    names, so it is the object the TOML codec writes and the Fluid Editor loads — the card and the full
//    editor are two views of one record rather than two records that have to be kept level.
export function ResolveGas(Values = {}, Children = []) {
  const Stored = { ...NewGasDomain(), ...Values };
  const Preset = PRESETS[Stored.Preset] || PRESETS.camp_fire;
  const Bounds = Array.isArray(Stored.Bounds) ? Stored.Bounds : [1.85, 2.1, 1.85];
  const Settings = { ...DEFAULT_PARAMS, ...(Preset.params || {}) };
  for (const Field of GasSheet)
    if (Stored[Field.Label] !== undefined) Settings[Field.Key] = Stored[Field.Label];
  Settings.boundsWidth = Bounds[0];
  Settings.boundsHeight = Bounds[1];

  // One emitter's readings reach the settings, because the simulator carries exactly one continuous source.
  //    The others are authored, listed and exported, and are told plainly that they are waiting on the
  //    native multi-emitter path rather than being silently dropped.
  const Live = Children.filter((Child) => Child.Values?.Enabled !== false);
  if (Live.length) {
    for (const Field of GasEmitterSheet)
      if (Live[0].Values?.[Field.Label] !== undefined) Settings[Field.Key] = Live[0].Values[Field.Label];
    Settings.emitterEnabled = true;
  } else if (Children.length) {
    Settings.emitterEnabled = false;
  }

  const Policy = GasRunPolicies.find((Entry) => Entry.Id === Stored.Run) || GasRunPolicies[0];
  const Tier = Stored.QualityPin === "auto" ? TierForDistance(Stored.Distance) : TierById(Stored.QualityPin);
  return {
    ...Stored,
    Bounds,
    PresetName: Preset.name,
    OneShot: Boolean(Preset.triggerExplosionOnLoad),
    Settings,
    Policy,
    Tier,
    Bytes: TierBytes(Tier),
    Hertz: TierHertz(Tier),
    Emitters: Children.length,
    LiveEmitters: Live.length,
    Carried: Live.length > 1 ? 1 : Live.length,
  };
}

// Is it simulating right now, in the editor's sense? The editor has no gameplay, so a dormant domain is
//    shown dormant rather than quietly run for the preview — showing a plume where the game shows nothing is
//    the most misleading thing this card could do.
export function GasRunning(Resolved, Fired = false) {
  if (Resolved.Tier.Id === "flipbook") return false;
  if (Resolved.Policy.Id === "always") return true;
  if (Resolved.Policy.Id === "proximity") return Resolved.Distance < 150;
  return Fired;
}

export function GasSummary(Resolved) {
  if (Resolved.Tier.Id === "flipbook")
    return "Beyond the live ladder · drawn as a flipbook card, no solver";
  if (Resolved.Policy.Id === "dormant") return "In the scene, not simulating · waits to be fired";
  if (Resolved.Policy.Id === "triggered")
    return GasRetires(Resolved)
      ? `Fires once · ${Number(Resolved.Lifetime || 0).toFixed(1)} s + ${GasFadeSeconds} s fade, then destroyed`
      : `Fires on trigger · runs ${Number(Resolved.Lifetime || 0).toFixed(1)} s, then sleeps holding its fields`;
  if (Resolved.Policy.Id === "proximity")
    return `Simulates inside 150 m · ${Resolved.Tier.Name} at ${Math.round(Resolved.Distance)} m`;
  return `Always on · ${Resolved.Tier.Name} tier at ${Resolved.Hertz} Hz`;
}

// A compact line for the outliner row, in the shape the wind and fog rows use.
export function GasRowSummary(Values = {}) {
  const Resolved = ResolveGas(Values);
  return `${Resolved.Bounds[0].toFixed(1)}×${Resolved.Bounds[1].toFixed(1)} m · ${Resolved.Tier.Name} · ${Resolved.Policy.Name.toLowerCase()}`;
}

export function GasEmitterRowSummary(Values = {}) {
  const Rate = Number(Values["Emission Rate"] ?? 1).toFixed(2);
  return `${Values.Enabled === false ? "off" : Rate + "×"} · ${Number(Values.Temperature ?? 0).toFixed(1)} K`;
}
