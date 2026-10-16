// Faceted fixture geometry is part of the ordinary scene: it casts sun/SDF
// shadows, enters the surface-card cache and follows the vehicle body transform.
// All dimensions are metres. Boxes are intentionally separate material layers:
// painted metal, rubber seals, polished reflectors and recessed LED dies.
import { CourseMaterial as M } from "./MaterialSpecification.js";
import { EmitCornerBoxFacets } from "./CourseStructure.js";
import { ResolveCoMAuthoredZ } from "./VehicleSpecification.js";

function Box(Facets, Minimum, Maximum, Family)
{
    Facets.push(...EmitCornerBoxFacets(Minimum, Maximum, Family));
}

// The existing archive's roof crown is z=1.352 in MODEL space at x≈0.25.
// Rebase with the body exactly once; do not attach this to a wheel or a camera.
export function ConstructVehicleLightingFacets()
{
    const Facets = [];
    for (const Side of [-1, 1])
    {
        const Y = Side * 0.54;
        Box(Facets, [0.06, Y - 0.075, 1.348], [0.41, Y + 0.075, 1.455], M.RoofMount);
        Box(Facets, [0.08, Y - 0.080, 1.451], [0.38, Y + 0.080, 1.469], M.LampGasket);
    }
    Box(Facets, [-0.01, -0.72, 1.46], [0.56, 0.72, 1.67], M.RoofHousing);
    // Extruded rear cooling fins; a shallow top visor prevents direct upward glare.
    for (let Y = -0.59; Y <= 0.60; Y += 0.17)
        Box(Facets, [-0.04, Y - 0.014, 1.50], [-0.012, Y + 0.014, 1.645], M.RoofReflector);
    Box(Facets, [0.48, -0.72, 1.67], [0.64, 0.72, 1.704], M.RoofHousing);
    Box(Facets, [0.559, -0.66, 1.476], [0.574, 0.66, 1.65], M.LampGasket);
    // Nine independently legible projector cells rather than one glowing slab.
    for (let Cell = -4; Cell <= 4; Cell++)
    {
        const Y = Cell * 0.139;
        Box(Facets, [0.575, Y - 0.061, 1.486], [0.594, Y + 0.061, 1.638], M.RoofReflector);
        Box(Facets, [0.595, Y - 0.052, 1.495], [0.602, Y + 0.052, 1.627], M.RoofLens);
        Box(Facets, [0.603, Y - 0.028, 1.540], [0.613, Y + 0.028, 1.582], M.RoofLED);
    }
    // Lower bumper-mounted fog projectors. The archive's original headlamps
    // (family Headlight) remain at the front and use the same lighting mode.
    for (const Side of [-1, 1])
    {
        const Y = Side * 0.89;
        Box(Facets, [2.99, Y - 0.13, 0.14], [3.165, Y + 0.13, 0.39], M.FogHousing);
        Box(Facets, [3.167, Y - 0.108, 0.163], [3.185, Y + 0.108, 0.363], M.RoofReflector);
        Box(Facets, [3.187, Y - 0.074, 0.205], [3.199, Y + 0.074, 0.327], M.FogLens);
    }
    const Rebase = ResolveCoMAuthoredZ();
    return Facets.map((Facet) => ({ ...Facet, Corners: Facet.Corners.map(([X, Y, Z]) => [X, Y, Z - Rebase]) }));
}

// A luminaire atop the existing scene pole. The pole and arm are raster-only
// colliders; the compact head is in the distance field and has surface cards.
// Its origin is the bottom centre, 10.8 m above the ground.
export function ConstructStreetPoleFacets()
{
    const Facets = [];
    Box(Facets, [-0.17, -0.17, 0], [0.17, 0.17, 10.86], M.LampPole);
    Box(Facets, [-0.25, -0.25, 0], [0.25, 0.25, 0.10], M.LampHousing);
    // Arm bridges the mast to the head at z=10.8 + 0.055 ... 0.275;
    // the last 55 mm overlap the casting instead of leaving a floating lamp.
    Box(Facets, [0.08, -0.08, 10.76], [0.80, 0.08, 10.91], M.LampPole);
    Box(Facets, [0.10, -0.11, 10.70], [0.22, 0.11, 10.83], M.LampGasket);
    return Facets;
}

export function ConstructStreetHeadFacets()
{
    const Facets = [];
    // Die-cast tapered-looking deck, black perimeter gasket, silver cavity.
    Box(Facets, [-0.87, -0.34, 0.055], [0.87, 0.34, 0.275], M.LampHousing);
    Box(Facets, [-0.94, -0.37, 0.273], [0.94, 0.37, 0.309], M.LampPole);
    Box(Facets, [-0.80, -0.30, 0.031], [0.80, 0.30, 0.060], M.LampGasket);
    Box(Facets, [-0.75, -0.26, 0.012], [0.75, 0.26, 0.030], M.LampReflector);
    // Frosted optical pockets under the housing; the warm dies are exposed
    // below the lip and face the roadway rather than shining through a box.
    for (const X of [-0.54, -0.18, 0.18, 0.54])
        for (const Y of [-0.14, 0.14])
        {
            Box(Facets, [X - 0.145, Y - 0.102, -0.011], [X + 0.145, Y + 0.102, 0.015], M.LampDiffuser);
            Box(Facets, [X - 0.058, Y - 0.044, -0.023], [X + 0.058, Y + 0.044, -0.010], M.LampWarm);
        }
    return Facets;
}
