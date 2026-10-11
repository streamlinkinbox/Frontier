// Native SolidArcEditorHost catalogue: identical section order, labels, defaults and ring placement.
const Point = (...Values) =>
  `(${Values.map((Value) => Number(Value.toFixed(6))).join(",")})`;
export const ConstructionCatalogue = [
  [
    "Reference",
    "Plane",
    "surface",
    "",
    (Horizontal, Vertical) =>
      `plane ${Point(Horizontal - 0.7, Vertical - 0.7, 0)} 1.4 1.4`,
  ],
  [
    "Sketch Draw",
    "Line",
    "edge",
    "L",
    (Horizontal, Vertical) =>
      `line ${Point(Horizontal - 0.7, Vertical - 0.5)} ${Point(Horizontal + 0.7, Vertical + 0.5)}`,
  ],
  [
    "Sketch Draw",
    "Polyline",
    "polyline",
    "",
    (Horizontal, Vertical) =>
      `polyline ${Point(Horizontal - 0.8, Vertical - 0.5)} ${Point(Horizontal - 0.3, Vertical + 0.5)} ${Point(Horizontal + 0.3, Vertical - 0.2)} ${Point(Horizontal + 0.8, Vertical + 0.5)}`,
  ],
  [
    "Sketch Draw",
    "Rectangle",
    "rectangle",
    "R",
    (Horizontal, Vertical) =>
      `rect ${Point(Horizontal - 0.7, Vertical - 0.5)} ${Point(Horizontal + 0.7, Vertical + 0.5)}`,
  ],
  [
    "Sketch Draw",
    "Centre Rect.",
    "centre",
    "",
    (Horizontal, Vertical) =>
      `rect ${Point(Horizontal, Vertical)} ${Point(Horizontal + 0.7, Vertical + 0.5)} --center`,
  ],
  [
    "Sketch Draw",
    "Slot",
    "slot",
    "",
    (Horizontal, Vertical) =>
      `slot ${Point(Horizontal - 0.5, Vertical)} ${Point(Horizontal + 0.5, Vertical)} .25`,
  ],
  [
    "Sketch Draw",
    "Circle",
    "circle",
    "C",
    (Horizontal, Vertical) => `circle ${Point(Horizontal, Vertical)} .6`,
  ],
  [
    "Sketch Draw",
    "Arc",
    "arc",
    "A",
    (Horizontal, Vertical) => `arc ${Point(Horizontal, Vertical)} .7 -60 240`,
  ],
  [
    "Sketch Draw",
    "Ellipse",
    "ellipse",
    "",
    (Horizontal, Vertical) => `ellipse ${Point(Horizontal, Vertical)} .8 .45`,
  ],
  [
    "Sketch Draw",
    "Polygon",
    "polygon",
    "P",
    (Horizontal, Vertical) => `polygon ${Point(Horizontal, Vertical)} .6 6`,
  ],
  [
    "Sketch Draw",
    "Spline",
    "curve",
    "",
    (Horizontal, Vertical) =>
      `spline ${Point(Horizontal - 0.8, Vertical - 0.3)} ${Point(Horizontal - 0.3, Vertical + 0.5)} ${Point(Horizontal + 0.3, Vertical - 0.5)} ${Point(Horizontal + 0.8, Vertical + 0.3)}`,
  ],
  [
    "Sketch Draw",
    "Control Curve",
    "controlcurve",
    "",
    (Horizontal, Vertical) =>
      `cpcurve ${Point(Horizontal - 0.8, Vertical - 0.3)} ${Point(Horizontal - 0.3, Vertical + 0.6)} ${Point(Horizontal + 0.3, Vertical - 0.6)} ${Point(Horizontal + 0.8, Vertical + 0.3)}`,
  ],
  [
    "Solid",
    "Box",
    "body",
    "",
    (Horizontal, Vertical) =>
      `box ${Point(Horizontal - 0.5, Vertical - 0.5, 0)} 1 1 1`,
  ],
  [
    "Solid",
    "Sphere",
    "sphere",
    "",
    (Horizontal, Vertical) => `sphere ${Point(Horizontal, Vertical, 0.6)} .6`,
  ],
  [
    "Solid",
    "Cylinder",
    "cylinder",
    "",
    (Horizontal, Vertical) =>
      `cylinder ${Point(Horizontal, Vertical, 0)} .45 1.2`,
  ],
  [
    "Solid",
    "Cone",
    "cone",
    "",
    (Horizontal, Vertical) =>
      `cone ${Point(Horizontal, Vertical, 0)} .6 .15 1.2`,
  ],
  [
    "Solid",
    "Torus",
    "ring",
    "",
    (Horizontal, Vertical) =>
      `torus ${Point(Horizontal, Vertical, 0.3)} .6 .25`,
  ],
  [
    "Surface",
    "Sphere Sheet",
    "sphere",
    "",
    (Horizontal, Vertical) =>
      `sphere ${Point(Horizontal, Vertical, 0.6)} .6 --sheet`,
  ],
  [
    "Surface",
    "Cylinder Sheet",
    "cylinder",
    "",
    (Horizontal, Vertical) =>
      `cylinder ${Point(Horizontal, Vertical, 0)} .45 1.2 --sheet`,
  ],
  [
    "Surface",
    "Cone Sheet",
    "cone",
    "",
    (Horizontal, Vertical) =>
      `cone ${Point(Horizontal, Vertical, 0)} .6 .15 1.2 --sheet`,
  ],
  [
    "Surface",
    "Torus Sheet",
    "ring",
    "",
    (Horizontal, Vertical) =>
      `torus ${Point(Horizontal, Vertical, 0.3)} .6 .25 --sheet`,
  ],
  [
    "Surface",
    "Patch",
    "cage",
    "",
    (Horizontal, Vertical) =>
      `patch 3 3 --degree=2 ${Array.from({ length: 9 }, (_, Index) => Point(Horizontal + ((Index % 3) - 1) * 0.7, Vertical + (Math.floor(Index / 3) - 1) * 0.7, Index === 4 ? 0.5 : 0)).join(" ")}`,
  ],
].map(([section, label, icon, key, command]) => ({
  section,
  label,
  icon,
  key,
  command,
}));
export function PlaceConstruction(Index, Slot) {
  const Radius = 2.6 + 0.9 * Math.floor(Slot / 8);
  const Angle = (((Slot % 8) + 0.5 * (Math.floor(Slot / 8) % 2)) * Math.PI) / 4;
  return ConstructionCatalogue[Index].command(
    Radius * Math.cos(Angle),
    Radius * Math.sin(Angle),
  );
}
