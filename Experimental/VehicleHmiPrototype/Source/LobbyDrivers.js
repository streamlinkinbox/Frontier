//============================================================================================================================================
//                                                         LOBBYDRIVERS.JS
//============================================================================================================================================
// 🧩 Who is driving each car in the studio, and the number plates that say so: every plate carries its driver's
//    nametag and their Rank — as a coloured badge with one chevron stripe per tier, plus the Rank in words.

import { CourseMaterial } from "./MaterialSpecification.js";
import { EmitQuad, EmitBoxFacets } from "./CourseStructure.js";
import { ControlVehicleSockets, ResolveCoMAuthoredZ } from "./VehicleSpecification.js";

// 📝 The field, in placement order: the hero (you) first, then the four display cars (north-east, north-west,
//    south-west, south-east). Edit names and ranks here; the plates rebuild themselves. Rank tiers, lowest to
//    highest: Bronze, Silver, Gold, Diamond — the badge wears the tier colour and that many chevron stripes.
export const LobbyDrivers = Object.freeze([
    Object.freeze({ NameTag: "NOVA-7",  Rank: "GOLD II" }),
    Object.freeze({ NameTag: "KESTREL", Rank: "DIAMOND I" }),
    Object.freeze({ NameTag: "MIRAGE",  Rank: "SILVER III" }),
    Object.freeze({ NameTag: "ORION",   Rank: "BRONZE I" }),
    Object.freeze({ NameTag: "VEGA-9",  Rank: "GOLD III" })
]);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLATE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 A 3 × 5 pixel letter, enough for a plate at showroom distance. Rows top to bottom, `#` is ink.
const Glyphs = Object.freeze({
    A: [".#.", "#.#", "###", "#.#", "#.#"], B: ["##.", "#.#", "##.", "#.#", "##."],
    C: [".##", "#..", "#..", "#..", ".##"], D: ["##.", "#.#", "#.#", "#.#", "##."],
    E: ["###", "#..", "##.", "#..", "###"], F: ["###", "#..", "##.", "#..", "#.."],
    G: [".##", "#..", "#.#", "#.#", ".##"], H: ["#.#", "#.#", "###", "#.#", "#.#"],
    I: ["###", ".#.", ".#.", ".#.", "###"], J: ["..#", "..#", "..#", "#.#", ".#."],
    K: ["#.#", "#.#", "##.", "#.#", "#.#"], L: ["#..", "#..", "#..", "#..", "###"],
    M: ["#.#", "###", "###", "#.#", "#.#"], N: ["#.#", "###", "###", "###", "#.#"],
    O: [".#.", "#.#", "#.#", "#.#", ".#."], P: ["##.", "#.#", "##.", "#..", "#.."],
    Q: [".#.", "#.#", "#.#", "##.", ".##"], R: ["##.", "#.#", "##.", "#.#", "#.#"],
    S: [".##", "#..", ".#.", "..#", "##."], T: ["###", ".#.", ".#.", ".#.", ".#."],
    U: ["#.#", "#.#", "#.#", "#.#", "###"], V: ["#.#", "#.#", "#.#", "#.#", ".#."],
    W: ["#.#", "#.#", "#.#", "###", "###"], X: ["#.#", "#.#", ".#.", "#.#", "#.#"],
    Y: ["#.#", "#.#", ".#.", ".#.", ".#."], Z: ["###", "..#", ".#.", "#..", "###"],
    "0": ["###", "#.#", "#.#", "#.#", "###"], "1": [".#.", "##.", ".#.", ".#.", "###"],
    "2": ["##.", "..#", ".#.", "#..", "###"], "3": ["##.", "..#", ".#.", "..#", "##."],
    "4": ["#.#", "#.#", "###", "..#", "..#"], "5": ["###", "#..", "##.", "..#", "##."],
    "6": [".##", "#..", "##.", "#.#", ".#."], "7": ["###", "..#", ".#.", ".#.", ".#."],
    "8": [".#.", "#.#", ".#.", "#.#", ".#."], "9": [".#.", "#.#", ".##", "..#", "##."],
    "-": ["...", "...", "###", "...", "..."], ".": ["...", "...", "...", "...", ".#."],
    " ": ["...", "...", "...", "...", "..."]
});

const TierFamilies = Object.freeze({
    BRONZE: CourseMaterial.RankBronze, SILVER: CourseMaterial.RankSilver,
    GOLD: CourseMaterial.RankGold, DIAMOND: CourseMaterial.RankDiamond
});
const TierChevrons = Object.freeze({ BRONZE: 1, SILVER: 2, GOLD: 3, DIAMOND: 4 });

// 📝 Both plates (front and rear, at the authored IdPlate sockets) for one driver, in the car's body frame
//    (centre of mass origin, facing +X). The face of each plate reads correctly to whoever faces it: lettering
//    runs along the viewer's right, which is +Y at the front and −Y at the back.
export function EmitLobbyPlateFacets(Driver)
{
    const Facets = [];
    const CoMZ = ResolveCoMAuthoredZ();
    const TierWord = String(Driver.Rank || "").trim().split(/\s+/)[0]?.toUpperCase() || "BRONZE";
    const BadgeFamily = TierFamilies[TierWord] ?? CourseMaterial.RankBronze;
    const Chevrons = TierChevrons[TierWord] ?? 1;

    const EmitPlate = (Socket, Outward) =>
    {
        // Outward: +1 front (+X face), −1 rear (−X face). Centre sits just proud of the bumper at the socket.
        const Centre = [Socket[0] + Outward * 0.02, Socket[1], Socket[2] - CoMZ];
        const Width = 0.56, Height = 0.15, Thickness = 0.016;

        // The plate itself: white face out, dark frame everywhere else.
        const FaceOut = Outward > 0 ? 2 : 3;   // EmitBoxFacets face order: top, bottom, +X, −X, +Y, −Y
        const Families = [CourseMaterial.StudioFrame, CourseMaterial.StudioFrame, CourseMaterial.StudioFrame,
                          CourseMaterial.StudioFrame, CourseMaterial.StudioFrame, CourseMaterial.StudioFrame];
        Families[FaceOut] = CourseMaterial.PlateFace;
        for (const Facet of EmitBoxFacets(Thickness, Width, Height, CourseMaterial.StudioFrame, Families))
            Facets.push({ Family: Facet.Family, Corners: Facet.Corners.map(([X, Y, Z]) =>
                [Centre[0] + X, Centre[1] + Y, Centre[2] - 0.5 * Height + Z]) });

        // Face coordinates: H runs along the viewer's right, V up. Both flip with the plate so text never mirrors.
        const FaceX = Centre[0] + Outward * (0.5 * Thickness + 0.0012);
        const Point = (H, V) => [FaceX, Centre[1] + Outward * H, Centre[2] + V];
        const Quad = (H0, V0, H1, V1, Family) =>
            EmitQuad(Facets, Point(H0, V0), Point(H1, V0), Point(H1, V1), Point(H0, V1), Family);
        const Pixel = (H, V, Size, Family) => Quad(H - 0.5 * Size, V - 0.5 * Size, H + 0.5 * Size, V + 0.5 * Size, Family);

        // The Rank badge: an octagon in the tier colour, a pale core, and one dark chevron stripe per tier.
        const BadgeH = -0.5 * Width + 0.052, BadgeRadius = 0.036;
        const Ring = (Radius, Family, Lift = 0.0) =>
        {
            const Middle = [FaceX + Outward * Lift, Centre[1] + Outward * BadgeH, Centre[2]];
            for (let Side = 0; Side < 8; ++Side)
            {
                const A0 = Math.PI * Side / 4.0, A1 = Math.PI * (Side + 1) / 4.0;
                Facets.push({ Family, Corners: [Middle,
                    Point(BadgeH + Radius * Math.cos(A0), Radius * Math.sin(A0)),
                    Point(BadgeH + Radius * Math.cos(A1), Radius * Math.sin(A1))] });
            }
        };
        Ring(BadgeRadius, BadgeFamily);
        Ring(BadgeRadius * 0.62, CourseMaterial.PlateFace, 0.0008);
        for (let Stripe = 0; Stripe < Chevrons; ++Stripe)
            Quad(BadgeH - 0.016 + Stripe * 0.009, -0.014, BadgeH - 0.011 + Stripe * 0.009, 0.014, CourseMaterial.PlateInk);

        // The lettering: nametag above, Rank below, both in plate ink starting right of the badge.
        const Letter = (Text, StartH, CentreV, Size) =>
        {
            const Advance = 4.0 * Size;
            String(Text).toUpperCase().split("").forEach((Character, Index) =>
            {
                const Glyph = Glyphs[Character] || Glyphs[" "];
                for (let Row = 0; Row < 5; ++Row)
                    for (let Column = 0; Column < 3; ++Column)
                        if (Glyph[Row][Column] === "#")
                            Pixel(StartH + Index * Advance + (Column - 1.0) * Size,
                                  CentreV + (2.0 - Row) * Size, Size * 0.92, CourseMaterial.PlateInk);
            });
        };
        Letter(Driver.NameTag, -0.5 * Width + 0.105,  0.031, 0.0115);
        Letter(Driver.Rank,    -0.5 * Width + 0.105, -0.036, 0.0085);
    };

    EmitPlate(ControlVehicleSockets.IdPlatePrimary, +1);
    EmitPlate(ControlVehicleSockets.IdPlateSecondary, -1);
    return Facets;
}
