export const Escape = (Text) =>
  String(Text).replace(
    /[&<>"']/g,
    (Character) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        Character
      ],
  );
export function NativeName(Text) {
  if (!Text.trim() || Text.length > 64 || /[;"\\\r\n]/.test(Text))
    throw new Error(
      "Use 1–64 characters, without quotes, semicolons or line breaks.",
    );
  return `"${Text.trim()}"`;
}
const NumberField = (
  key,
  label,
  initial,
  min = 0.001,
  max = 100,
  step = 0.01,
  unit = "m",
) => ({ key, label, initial, min, max, step, unit });
const NameField = { key: "name", label: "Name", initial: "", text: true };
const Position = [
  NumberField("x", "Position X", 0, -100),
  NumberField("y", "Position Y", 0, -100),
  NumberField("z", "Position Z", 0, -100),
];
const Point = (Settings) => `(${Settings.x},${Settings.y},${Settings.z})`;
const Name = (Settings) =>
  Settings.name ? ` --name=${NativeName(Settings.name)}` : "";
export const Tools = {
  box: {
    title: "Box",
    icon: "body",
    group: "Solids",
    description: "Construct a native six-face solid. Dimensions are in metres.",
    fields: [
      NameField,
      NumberField("width", "Width", 2),
      NumberField("depth", "Depth", 1.5),
      NumberField("height", "Height", 1),
      ...Position,
    ],
    command: (Settings) =>
      `box ${Point(Settings)} ${Settings.width} ${Settings.depth} ${Settings.height}${Name(Settings)}`,
  },
  cylinder: {
    title: "Cylinder",
    icon: "cylinder",
    group: "Solids",
    description: "An exact cylindrical body with planar end caps.",
    fields: [
      NameField,
      NumberField("radius", "Radius", 0.5),
      NumberField("height", "Height", 1.5),
      ...Position,
    ],
    command: (Settings) =>
      `cylinder ${Point(Settings)} ${Settings.radius} ${Settings.height}${Name(Settings)}`,
  },
  sphere: {
    title: "Sphere",
    icon: "sphere",
    group: "Solids",
    description: "A native spherical solid with rational surface geometry.",
    fields: [NameField, NumberField("radius", "Radius", 0.75), ...Position],
    command: (Settings) =>
      `sphere ${Point(Settings)} ${Settings.radius}${Name(Settings)}`,
  },
  torus: {
    title: "Torus",
    icon: "ring",
    group: "Solids",
    description:
      "A toroidal solid. The major radius must exceed the tube radius.",
    fields: [
      NameField,
      NumberField("major", "Major radius", 1),
      NumberField("minor", "Tube radius", 0.2),
      ...Position,
    ],
    command: (Settings) =>
      `torus ${Point(Settings)} ${Settings.major} ${Settings.minor}${Name(Settings)}`,
  },
  rect: {
    title: "Rectangle",
    icon: "surface",
    group: "Sketches",
    description:
      "A closed profile on the active workplane, optionally with rounded corners.",
    fields: [
      NameField,
      NumberField("width", "Width", 2),
      NumberField("height", "Height", 1.5),
      NumberField("radius", "Corner radius", 0.15, 0),
    ],
    command: (Settings) =>
      `rect (0,0) (${Settings.width},${Settings.height}) --radius=${Settings.radius}${Name(Settings)}`,
  },
  circle: {
    title: "Circle",
    icon: "circle",
    group: "Sketches",
    description: "An exact circular profile on the active workplane.",
    fields: [NameField, NumberField("radius", "Radius", 0.75)],
    command: (Settings) => `circle (0,0) ${Settings.radius}${Name(Settings)}`,
  },
  plane: {
    title: "Plane",
    icon: "surface",
    group: "Surfaces",
    description:
      "Construct an editable planar surface on the active workplane.",
    fields: [
      NameField,
      NumberField("width", "Width", 2),
      NumberField("height", "Height", 2),
      ...Position,
    ],
    command: (Settings) =>
      `plane ${Point(Settings)} ${Settings.width} ${Settings.height}${Name(Settings)}`,
  },
  spline: {
    title: "Spline",
    icon: "curve",
    group: "Sketches",
    description:
      "Interpolate a four-point spatial curve. Edit the native command for more points.",
    fields: [
      NameField,
      NumberField("span", "Span", 3),
      NumberField("rise", "Rise", 1, -10, 10),
    ],
    command: (Settings) =>
      `spline (0,0,0) (${Settings.span / 3},${Settings.rise},0) (${(Settings.span * 2) / 3},${-Settings.rise},0) (${Settings.span},0,0)${Name(Settings)}`,
  },
  extrude: {
    title: "Extrude",
    icon: "extrude",
    group: "Operations",
    description:
      "Select one closed sketch or open curve. Closed profiles form solids; open curves form sheets.",
    fields: [NumberField("distance", "Distance", 1, -100)],
    command: (Settings, Picked) =>
      `extrude ${Require(Picked, 1)[0].id} ${Settings.distance}`,
  },
  revolve: {
    title: "Revolve",
    icon: "rotate",
    group: "Operations",
    description:
      "Revolve one selected profile around world Z. Use a profile on the XZ workplane for a conventional turned body.",
    fields: [NumberField("angle", "Angle", 360, 1, 360, 1, "°")],
    command: (Settings, Picked) =>
      `revolve ${Require(Picked, 1)[0].id} ${Settings.angle} --axis=(0,0,1)`,
  },
  loft: {
    title: "Loft",
    icon: "layers",
    group: "Operations",
    description:
      "Select at least two profiles. Sections are supplied in document order; use Commands for explicit ordering.",
    fields: [],
    command: (Settings, Picked) =>
      `loft ${Require(Picked, 2)
        .map((Figure) => Figure.id)
        .join(" ")}`,
  },
  fillet: {
    title: "Fillet",
    icon: "fillet",
    group: "Operations",
    description:
      "Select body edges in edge mode, then choose a radius. Native support is bounded; unsupported geometry refuses with a reason.",
    fields: [NumberField("radius", "Radius", 0.1, 0.001, 10)],
    command: (Settings, Picked) => {
      const Figure = Require(Picked, 1)[0];
      if (!Figure.pickedEdges.length)
        throw new Error(
          "Select at least one body edge in the viewport first (edge mode · 2).",
        );
      return `fillet ${Figure.id} ${Settings.radius} --edges=${Figure.pickedEdges.join(",")}`;
    },
  },
  chamfer: {
    title: "Chamfer",
    icon: "chamfer",
    group: "Operations",
    description:
      "Select one or more body edges. The native solver validates the supported edge set before applying.",
    fields: [NumberField("distance", "Setback", 0.1, 0.001, 10)],
    command: (Settings, Picked) => {
      const Figure = Require(Picked, 1)[0];
      if (!Figure.pickedEdges.length)
        throw new Error("Select body edges in edge mode first.");
      return `chamfer ${Figure.id} ${Settings.distance} --edges=${Figure.pickedEdges.join(",")}`;
    },
  },
  shell: {
    title: "Shell",
    icon: "body",
    group: "Operations",
    description:
      "Select one face of a canonical box. That face becomes the opening. Arbitrary curved shells are not supported by this native route.",
    fields: [NumberField("thickness", "Thickness", 0.1, 0.001, 10)],
    command: (Settings, Picked) => {
      const Figure = Require(Picked, 1)[0];
      if (Figure.pickedFaces.length !== 1)
        throw new Error("Select exactly one opening face in face mode first.");
      return `shell ${Figure.id} ${Settings.thickness} --face=${Figure.pickedFaces[0]}`;
    },
  },
  move: {
    title: "Move",
    icon: "move",
    group: "Operations",
    description: "Translate the selected geometry by an exact displacement.",
    fields: [
      NumberField("x", "Displacement X", 0, -100),
      NumberField("y", "Displacement Y", 0, -100),
      NumberField("z", "Displacement Z", 0, -100),
    ],
    command: (Settings, Picked) =>
      `move ${Require(Picked, 1)
        .map((Figure) => Figure.id)
        .join(" ")} ${Point(Settings)}`,
  },
  union: {
    title: "Union",
    icon: "union",
    group: "Operations",
    description:
      "Combine exactly two selected bodies. The native kernel may refuse unsupported intersection configurations.",
    fields: [],
    command: (Settings, Picked) => BooleanCommand("union", Picked),
  },
  subtract: {
    title: "Subtract",
    icon: "subtract",
    group: "Operations",
    description:
      "Subtract the second selected body (document order) from the first. Use Commands to specify a different order.",
    fields: [],
    command: (Settings, Picked) => BooleanCommand("subtract", Picked),
  },
  intersect: {
    title: "Intersect",
    icon: "intersect",
    group: "Operations",
    description:
      "Retain the shared volume of exactly two selected bodies. Refusals are reported by the native kernel.",
    fields: [],
    command: (Settings, Picked) => BooleanCommand("intersect", Picked),
  },
};
function Require(Picked, Minimum) {
  if (Picked.length < Minimum)
    throw new Error(
      `Select at least ${Minimum} geometry figure${Minimum > 1 ? "s" : ""} first.`,
    );
  return Picked;
}
function BooleanCommand(Operation, Picked) {
  if (
    Picked.length !== 2 ||
    Picked.some((Figure) => Figure.classification !== 2)
  )
    throw new Error("Select exactly two solid bodies using Shift-click.");
  return `boolean ${Operation} ${Picked[0].id} -- ${Picked[1].id}`;
}
export function ConstructCommand(Tool, Settings, Picked) {
  const Definition = Tools[Tool];
  if (!Definition) throw new Error("Unknown construction tool.");
  for (const Field of Definition.fields)
    if (
      !Field.text &&
      (!Number.isFinite(Settings[Field.key]) ||
        Settings[Field.key] < Field.min ||
        Settings[Field.key] > Field.max)
    )
      throw new Error(
        `${Field.label} must be between ${Field.min} and ${Field.max}.`,
      );
  if (Tool === "torus" && Settings.minor >= Settings.major)
    throw new Error("Tube radius must be smaller than the major radius.");
  if (
    ["extrude", "revolve", "fillet", "chamfer", "shell"].includes(Tool) &&
    Picked.length !== 1
  )
    throw new Error(
      "Select exactly one figure or sketch region for this operation.",
    );
  return Definition.command(Settings, Picked);
}
export const Examples = {
  mount: {
    name: "Mount study",
    description: "Rounded base, turned housing and four locating pins.",
    icon: "body",
    commands: [
      "rect (-2,-1.4) (2,1.4) --radius=0.25 --name=Base_profile",
      "extrude Base_profile 0.24",
      "rename Extrusion Base_plate",
      "hide Base_profile",
      "cylinder (0,0,0.24) 0.66 0.85 --name=Housing",
      "cylinder (0,0,0.18) 0.32 1.1 --name=Bore",
      "boolean subtract Housing -- Bore --name=Socket",
      "cylinder (-1.45,-0.9,0.24) 0.15 0.15 --name=Pin_01",
      "cylinder (1.45,-0.9,0.24) 0.15 0.15 --name=Pin_02",
      "cylinder (1.45,0.9,0.24) 0.15 0.15 --name=Pin_03",
      "cylinder (-1.45,0.9,0.24) 0.15 0.15 --name=Pin_04",
      "matcap all steel",
      "show cages off",
      "show iso off",
      "show edges on",
      "dim hide all",
      "view iso",
      "view fit",
      "select Base_plate",
    ],
  },
  turned: {
    name: "Turned forms",
    description:
      "Exact primitive bodies with live radius and height dimensions.",
    icon: "cylinder",
    commands: [
      "cylinder (-1.5,0,0) 0.65 1.5 --name=Spindle",
      "torus (0.6,0,0.3) 0.65 0.2 --name=Collar",
      "sphere (2.2,0,0.6) 0.6 --name=Ball",
      "matcap all steel",
      "show cages off",
      "show edges on",
      "dim hide all",
      "view iso",
      "view fit",
      "select Spindle",
    ],
  },
  sketch: {
    name: "Profile workshop",
    description: "Closed and open curves ready to extrude, loft or revolve.",
    icon: "curve",
    commands: [
      "rect (-2,-1) (0,1) --radius=0.2 --name=Rounded_profile",
      "circle (1.5,0) 0.8 --name=Ring_profile",
      "spline (-2,2) (-0.5,3) (1,2) (2.5,3) --name=Guide",
      "show cages off",
      "dim hide all",
      "view top",
      "view fit",
      "select Rounded_profile",
    ],
  },
  boolean: {
    name: "Boolean trial",
    description:
      "Two overlapping native boxes. Shift-select both, then try Union or Subtract.",
    icon: "subtract",
    commands: [
      "box (-1.5,-1,0) (0.7,1,1.6) --name=Stock",
      "box (0,-0.65,0.5) (1.5,0.65,2) --name=Tool",
      "matcap Stock steel",
      "matcap Tool copper",
      "show cages off",
      "dim hide all",
      "view iso",
      "view fit",
      "select Stock",
    ],
  },
};
