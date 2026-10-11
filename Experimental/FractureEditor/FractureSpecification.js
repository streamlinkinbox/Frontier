//============================================================================================================================================
//                                                          FRACTURESPECIFICATION.JS
//============================================================================================================================================
// 📦 Per-scene-ID fracture recipes and geometry-sensitive browser correspondence.

// HTML authoring records are owned by scene IDs, never by material names.
export const Prefix = "Frontier.Fracture.v2:";
export const Defaults = {
  Enabled: false,
  Mode: "dynamic",
  PieceSdf: false,
  SdfResolution: 64,
  Material: "concrete",
  Energy: 2500,
  Seed: 42,
  Ceiling: 48,
  MinimumSize: 0.045,
  X: 0,
  Y: 0,
  Z: 0,
};
export const MaterialNames = {
  concrete: "Concrete",
  rock: "Stone",
  wood: "Wood",
  glass: "Glass",
  tempered: "Tempered glass",
  plastic: "ABS plastic",
};
const Limits = {
  Energy: [0, 50000],
  Seed: [1, 999999],
  Ceiling: [2, 160],
  MinimumSize: [0.002, 0.3],
  X: [-1000, 1000],
  Y: [-1000, 1000],
  Z: [-1000, 1000],
};
export function Normalize(Input = {}) {
  if (!Input || typeof Input !== "object") Input = {};
  const Result = {
    ...Defaults,
    EditedAt: Number.isFinite(Input.EditedAt) ? Input.EditedAt : 0,
    Enabled: Input.Enabled === true,
    PieceSdf: Input.PieceSdf === true,
    SdfResolution: [32, 64, 128].includes(Number(Input.SdfResolution))
      ? Number(Input.SdfResolution)
      : 64,
    Mode: Input.Mode === "baked" ? "baked" : "dynamic",
    Material: Object.hasOwn(MaterialNames, Input.Material)
      ? Input.Material
      : Defaults.Material,
  };
  for (const [Key, [Min, Max]] of Object.entries(Limits)) {
    const NumberValue = Number(Input[Key] ?? Defaults[Key]);
    Result[Key] = Number.isFinite(NumberValue)
      ? Math.min(Max, Math.max(Min, NumberValue))
      : Defaults[Key];
    if (["Ceiling", "Seed"].includes(Key))
      Result[Key] = Math.round(Result[Key]);
  }
  if (Input.Baked && typeof Input.Baked.Signature === "string")
    Result.Baked = Input.Baked;
  return Result;
}
export function Describe(Subject, Values = {}) {
  return {
    Id: Subject.Id,
    Name: Subject.Name,
    Primitive: Subject.Icon?.replace(/^editor-/, ""),
    Scale: [0, 1, 2].map((Axis) => {
      const Coordinate = Number(Values.Scale?.[Axis] ?? 1);
      return Number.isFinite(Coordinate)
        ? Math.min(1000, Math.max(0.001, Coordinate))
        : 1;
    }),
  };
}
export function Signature(Owner, Settings) {
  const { Enabled, Mode, Baked, EditedAt, PieceSdf, SdfResolution, ...Recipe } =
    Normalize(Settings);
  return JSON.stringify({
    Revision: 2,
    Primitive: Owner.Primitive,
    Scale: Owner.Scale,
    Recipe,
  });
}
export function ReadRecord(Id) {
  try {
    return JSON.parse(localStorage.getItem(Prefix + Id));
  } catch {
    return null;
  }
}
export function Publish(Owner, Settings) {
  const Record = { Owner, Settings: Normalize(Settings) },
    Text = JSON.stringify(Record),
    Key = Prefix + Owner.Id;
  if (localStorage.getItem(Key) !== Text) localStorage.setItem(Key, Text);
  return Record;
}
export function OpenFracture(Subject, Values) {
  Publish(Describe(Subject, Values), Values.Fracture);
  window.open(
    "../FractureEditor/index.html?object=" + encodeURIComponent(Subject.Id),
    "_blank",
    "noopener",
  );
}
