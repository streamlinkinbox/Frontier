/*====================================================================================================================================
                                                        LINKROUTING.JS
====================================================================================================================================*/
// 🧩 Orthogonal link routing with filleted corners — the smoothstep curve between two ports

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const CornerRadius       = 8;     // [px] - Fillet radius applied at each direction change
const MinimumProjection  = 20;    // [px] - Shortest straight run leaving a port before the first turn

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Emit a filleted corner at Pivot arriving from Preceding and departing toward Following. The fillet
//    is a quadratic whose control point is the corner itself, entered and left along each leg.
function InscribeFilletedCorner(Preceding, Pivot, Following)
{
    const IncomingLength = Math.hypot(Pivot.X - Preceding.X, Pivot.Y - Preceding.Y);
    const OutgoingLength = Math.hypot(Following.X - Pivot.X, Following.Y - Pivot.Y);
    const AppliedRadius  = Math.min(CornerRadius, IncomingLength / 2, OutgoingLength / 2);

    if (AppliedRadius < 0.5) return `L ${Pivot.X} ${Pivot.Y}`;

    const IncomingUnitX = (Pivot.X - Preceding.X) / IncomingLength;
    const IncomingUnitY = (Pivot.Y - Preceding.Y) / IncomingLength;
    const OutgoingUnitX = (Following.X - Pivot.X) / OutgoingLength;
    const OutgoingUnitY = (Following.Y - Pivot.Y) / OutgoingLength;

    const EntryX = Pivot.X - IncomingUnitX * AppliedRadius;
    const EntryY = Pivot.Y - IncomingUnitY * AppliedRadius;
    const ExitX  = Pivot.X + OutgoingUnitX * AppliedRadius;
    const ExitY  = Pivot.Y + OutgoingUnitY * AppliedRadius;

    return `L ${EntryX} ${EntryY} Q ${Pivot.X} ${Pivot.Y} ${ExitX} ${ExitY}`;
}

// Derive the orthogonal waypoint sequence from an outbound port to an inbound port.
function DeriveWaypoints(OriginX, OriginY, ArrivalX, ArrivalY)
{
    const Origin  = { X: OriginX,  Y: OriginY };
    const Arrival = { X: ArrivalX, Y: ArrivalY };

    const HorizontalSpan = ArrivalX - OriginX;

    // 📝 When the target sits comfortably to the right, two turns suffice: run out, cross at the
    //    midpoint, run in. Otherwise the path must loop back around both ports.
    if (HorizontalSpan >= MinimumProjection * 2)
    {
        const CrossingX = OriginX + HorizontalSpan / 2;
        return [
            Origin,
            { X: CrossingX, Y: OriginY },
            { X: CrossingX, Y: ArrivalY },
            Arrival
        ];
    }

    const OutwardX = OriginX + MinimumProjection;
    const InwardX  = ArrivalX - MinimumProjection;
    const CrossingY = (OriginY + ArrivalY) / 2;

    return [
        Origin,
        { X: OutwardX, Y: OriginY },
        { X: OutwardX, Y: CrossingY },
        { X: InwardX,  Y: CrossingY },
        { X: InwardX,  Y: ArrivalY },
        Arrival
    ];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Formulate the SVG path data for a link between two plane-space port coordinates.
export function FormulateLinkPath(OriginX, OriginY, ArrivalX, ArrivalY)
{
    const Waypoints = DeriveWaypoints(OriginX, OriginY, ArrivalX, ArrivalY);

    // 📝 Collapse waypoints that coincide, else a zero-length leg divides by zero in the fillet.
    const Distinct = Waypoints.filter((Waypoint, Index) =>
        Index === 0 || Math.hypot(Waypoint.X - Waypoints[Index - 1].X, Waypoint.Y - Waypoints[Index - 1].Y) > 0.01);

    if (Distinct.length < 2) return `M ${OriginX} ${OriginY} L ${ArrivalX} ${ArrivalY}`;

    let PathData = `M ${Distinct[0].X} ${Distinct[0].Y}`;
    for (let PivotIndex = 1; PivotIndex < Distinct.length - 1; PivotIndex++)
    {
        PathData += ' ' + InscribeFilletedCorner(
            Distinct[PivotIndex - 1], Distinct[PivotIndex], Distinct[PivotIndex + 1]);
    }
    const Terminal = Distinct[Distinct.length - 1];
    PathData += ` L ${Terminal.X} ${Terminal.Y}`;

    return PathData;
}
