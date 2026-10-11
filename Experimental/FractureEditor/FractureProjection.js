//============================================================================================================================================
//                                                           FRACTUREPROJECTION.JS
//============================================================================================================================================
// 📦 Shaded fracture illustrations for compact inspector cards; independent of simulation geometry.

const PointsText = (Points) =>
  Points.map((Point) =>
    Point.map((Coordinate) => Coordinate.toFixed(2)).join(","),
  ).join(" ");
function Clip(Polygon, Direction, Distance) {
  const Clipped = [];
  Polygon.forEach((Point, Index) => {
    const Next = Polygon[(Index + 1) % Polygon.length];
    const Offset = Point[0] * Direction[0] + Point[1] * Direction[1] - Distance;
    const NextOffset =
      Next[0] * Direction[0] + Next[1] * Direction[1] - Distance;
    if (Offset <= 0) Clipped.push(Point);
    if (Offset < 0 !== NextOffset < 0) {
      const Fraction = Offset / (Offset - NextOffset);
      Clipped.push(
        Point.map(
          (Coordinate, Axis) =>
            Coordinate + (Next[Axis] - Coordinate) * Fraction,
        ),
      );
    }
  });
  return Clipped;
}
function Cells(Sites, Perimeter) {
  return Sites.map((Site, Index) => {
    let Polygon = Perimeter;
    Sites.forEach((Other, Slot) => {
      if (Slot === Index) return;
      const Direction = Other.map(
        (Coordinate, Axis) => Coordinate - Site[Axis],
      );
      const Distance =
        (Other[0] ** 2 + Other[1] ** 2 - Site[0] ** 2 - Site[1] ** 2) / 2;
      Polygon = Clip(Polygon, Direction, Distance);
    });
    return Polygon;
  });
}
function Shade(Tone, Green = false) {
  const Level = Math.round(Math.max(35, Math.min(210, Tone)));
  return Green
    ? `rgb(${Level - 12},${Level + 3},${Level - 4})`
    : `rgb(${Level},${Level + 2},${Level + 1})`;
}
function Facet(Polygon, Index, Project) {
  if (Polygon.length < 3) return "";
  const Center = Polygon.reduce(
    (Sum, Point) =>
      Sum.map((Coordinate, Axis) => Coordinate + Point[Axis] / Polygon.length),
    [0, 0],
  );
  const Surface = Polygon.map((Point) => Project(Point, Center));
  const Interior = Project(Center, Center);
  const Tone = 133 - Center[0] * 0.36 - Center[1] * 0.35;
  let Markup = "";
  Surface.forEach((Point, Slot) => {
    const Next = Surface[(Slot + 1) % Surface.length];
    if (Next[0] < Point[0])
      Markup += `<polygon points="${PointsText([Point, Next, [Next[0] + 1, Next[1] + 7], [Point[0] + 1, Point[1] + 7]])}" fill="${Shade(Tone * 0.42)}"/>`;
  });
  Surface.forEach((Point, Slot) => {
    const Next = Surface[(Slot + 1) % Surface.length];
    Markup += `<polygon points="${PointsText([Point, Next, Interior])}" fill="${Shade(Tone + Math.sin(Slot * 2.3 + Index) * 15, Index % 7 === 0)}"/>`;
  });
  return (
    Markup +
    `<polygon points="${PointsText(Surface)}" fill="none" stroke="#d4dfd4" stroke-opacity=".19" stroke-width=".6" stroke-linejoin="round"/>`
  );
}
export function FractureGlyph() {
  const Sites = [
    [-9, -6],
    [19, -22],
    [-32, -29],
    [41, 5],
    [-34, 8],
    [8, 28],
    [-19, 37],
    [39, 39],
    [2, -46],
  ];
  const Perimeter = Array.from({ length: 36 }, (_, Index) => {
    const Angle = (Index * Math.PI) / 18;
    return [Math.cos(Angle) * 58, Math.sin(Angle) * 58];
  });
  const Pieces = Cells(Sites, Perimeter).sort(
    (First, Second) => First[0][1] - Second[0][1],
  );
  return `<svg viewBox="0 0 280 134" role="img" aria-label="Shaded fractured solid illustration"><ellipse cx="140" cy="113" rx="71" ry="9" fill="#000" opacity=".19"/><g>${Pieces.map((Polygon, Index) => Facet(Polygon, Index, (Point, Center) => [140 + Point[0] * 1.05 + Center[0] * 0.18, 61 + Point[1] * 0.77 + Center[1] * 0.16 - Math.sqrt(Math.max(0, 58 ** 2 - Center[0] ** 2 - Center[1] ** 2)) * 0.12])).join("")}</g></svg>`;
}
export function QualityGlyph(Ceiling, MinimumSize) {
  const Count = Math.max(
    6,
    Math.min(28, Math.round(6 + Math.sqrt(Ceiling) * 1.6 - MinimumSize * 30)),
  );
  const Sites = Array.from({ length: Count }, (_, Index) => [
    ((Index * 0.61803398875 + 0.1) % 1) * 226 - 113,
    ((Index * 0.41421356237 + 0.17) % 1) * 69 - 34.5,
  ]);
  const Pieces = Cells(Sites, [
    [-114, -36],
    [114, -36],
    [114, 36],
    [-114, 36],
  ]);
  const Gap = 2 + MinimumSize * 10;
  return `<svg viewBox="0 0 280 126" role="img" aria-label="Illustrative fragment sizing preview"><ellipse cx="140" cy="107" rx="110" ry="8" fill="#000" opacity=".16"/>${Pieces.map(
    (Polygon, Index) =>
      Facet(Polygon, Index, (Point, Center) => {
        const Delta = Point.map(
          (Coordinate, Axis) => Coordinate - Center[Axis],
        );
        const Length = Math.hypot(...Delta) || 1;
        return [
          140 + Center[0] + Delta[0] * Math.max(0.35, 1 - Gap / Length),
          60 + Center[1] + Delta[1] * Math.max(0.35, 1 - Gap / Length),
        ];
      }),
  ).join("")}</svg>`;
}
