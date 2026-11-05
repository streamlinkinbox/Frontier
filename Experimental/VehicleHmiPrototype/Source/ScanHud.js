//============================================================================================================================================
//                                                              SCANHUD.JS
//============================================================================================================================================
// 🧩 The scan's DOM overlay, ported from interactive-vehicle-hmi-configurator.zip (App.tsx "scan-mode HUD overlays" and Truck.tsx <Tag>) onto
//    a 2D canvas that the Garage uploads as a texture: the four labelled anchors, the corner frame and its two captions.
//    The canvas is two stacked layers, as the page stacks them: the tags above (beneath the vignette), the frame below (above the dot matrix).
//
//    Everything that moves is a CSS animation in the reference, evaluated here by the same keyframes and timing functions:
//    animate-ping (1 s, cubic-bezier(0, 0, 0.2, 1)), animate-pulse (2 s, cubic-bezier(0.4, 0, 0.6, 1)) and the scan line's framer-motion
//    keyframes (7 s, easeInOut = cubic-bezier(0.42, 0, 0.58, 1)).

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

export const HudSpecification = Object.freeze({
    FontFamily: "system-ui, -apple-system, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif",   // Tailwind's font-sans
    LineHeight: 1.5,                                                                           // Tailwind preflight, inherited by text-[8px]
    // 📝 absolute inset-x-10 top-16 bottom-28, in the reference's CSS pixels.
    Inset:      Object.freeze({ Left: 40, Right: 40, Top: 64, Bottom: 112 }),
    Bracket:    28,    // [px] h-7 w-7
    BracketLine: 2,    // [px] border-l-2 …
    Caption:    Object.freeze({ Size: 10, Spacing: 3.0, Rise: 24, Gap: 8, DotSize: 6 }),   // text-[10px] tracking-[0.3em] -top-6 gap-2 h-1.5 w-1.5
    Tag:        Object.freeze({
        ShiftX: 8, Gap: 8, Dot: 8, Ring: 2, LineWidth: 28, PaddingX: 10, PaddingY: 6, Radius: 8,
        LabelSize: 8, LabelSpacing: 1.76, ValueSize: 10, RingWidth: 1
    }),
    Colours:    Object.freeze({
        Cyan100: [207, 250, 254], Cyan200: [165, 243, 252], Cyan300: [103, 232, 249], Black: [0, 0, 0], White: [255, 255, 255]
    }),
    PingSeconds: 1.0, PulseSeconds: 2.0, LineSeconds: 7.0,
    LineTop:    [0.08, 0.92]
});

// 📝 The tags exactly as Truck.tsx writes them; the anchors are the reference truck's, moved onto this car: x along, y across (positive to the
//    car's right), z above the ground [m].
export const ScanTags = Object.freeze([
    Object.freeze({ Label: "Front Motor",          Value: "500 kW · e-diff lock",      Anchor: [2.35, 0.0, 1.1] }),
    Object.freeze({ Label: "Ultium Battery Pack",  Value: "212 kWh · 329 mi range",    Anchor: [-0.5, 0.0, 0.42] }),
    Object.freeze({ Label: "35-in All-Terrain",    Value: "TPMS 38 PSI · OK",          Anchor: [1.7274, 1.0475, 0.5085] }),
    Object.freeze({ Label: "Cabin",                Value: "5 seats · panoramic glass", Anchor: [-0.12, 0.0, 1.77] })
]);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CSS TIMING
//------------------------------------------------------------------------------------------------------------------------

// 📐 cubic-bezier(x1, y1, x2, y2): the curve's y at progress x, found by bisection on the parametric x (the browser's own definition).
export function EvaluateCubicBezier(X1, Y1, X2, Y2, X)
{
    if (X <= 0.0) return 0.0;
    if (X >= 1.0) return 1.0;
    const Sample = (T, A, B) => 3.0 * A * (1.0 - T) * (1.0 - T) * T + 3.0 * B * (1.0 - T) * T * T + T * T * T;
    let Low = 0.0, High = 1.0, T = X;
    for (let Step = 0; Step < 40; ++Step)
    {
        T = 0.5 * (Low + High);
        if (Sample(T, X1, X2) < X) Low = T; else High = T;
    }
    return Sample(T, Y1, Y2);
}

// 📝 animate-ping: @keyframes ping { 75%, 100% { transform: scale(2); opacity: 0 } }, one second, cubic-bezier(0, 0, 0.2, 1) per interval.
export function ResolvePing(Seconds)
{
    const Phase = (((Seconds / HudSpecification.PingSeconds) % 1.0) + 1.0) % 1.0;
    if (Phase >= 0.75)
    {
        return { Scale: 2.0, Opacity: 0.0 };
    }
    const Eased = EvaluateCubicBezier(0.0, 0.0, 0.2, 1.0, Phase / 0.75);
    return { Scale: 1.0 + Eased, Opacity: 1.0 - Eased };
}

// 📝 animate-pulse: @keyframes pulse { 50% { opacity: .5 } }, two seconds, cubic-bezier(0.4, 0, 0.6, 1) per interval.
export function ResolvePulse(Seconds)
{
    const Phase = (((Seconds / HudSpecification.PulseSeconds) % 1.0) + 1.0) % 1.0;
    if (Phase < 0.5)
    {
        return 1.0 - 0.5 * EvaluateCubicBezier(0.4, 0.0, 0.6, 1.0, Phase / 0.5);
    }
    return 0.5 + 0.5 * EvaluateCubicBezier(0.4, 0.0, 0.6, 1.0, (Phase - 0.5) / 0.5);
}

// 📝 framer-motion animate={{ top: ["8%", "92%", "8%"] }}, duration 7, ease "easeInOut", repeat Infinity: even times, the ease per interval.
export function ResolveScanLineTop(Seconds)
{
    const [From, To] = HudSpecification.LineTop;
    const Phase = (((Seconds / HudSpecification.LineSeconds) % 1.0) + 1.0) % 1.0;
    const Local = Phase < 0.5 ? Phase / 0.5 : (Phase - 0.5) / 0.5;
    const Eased = EvaluateCubicBezier(0.42, 0.0, 0.58, 1.0, Local);
    return Phase < 0.5 ? From + (To - From) * Eased : To - (To - From) * Eased;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LAYOUT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The corner frame's box in the stage's pixels.
export function ResolveFrameBox(Width, Height)
{
    const Inset = HudSpecification.Inset;
    return { X: Inset.Left, Y: Inset.Top, Width: Math.max(0, Width - Inset.Left - Inset.Right), Height: Math.max(0, Height - Inset.Top - Inset.Bottom) };
}

// 📐 A world point in the stage's pixels (y down), or null behind the camera or beyond the clip volume's depth.
export function ProjectToStage(ViewProjection, Point, Width, Height)
{
    const X = ViewProjection[0] * Point[0] + ViewProjection[4] * Point[1] + ViewProjection[8] * Point[2] + ViewProjection[12];
    const Y = ViewProjection[1] * Point[0] + ViewProjection[5] * Point[1] + ViewProjection[9] * Point[2] + ViewProjection[13];
    const W = ViewProjection[3] * Point[0] + ViewProjection[7] * Point[1] + ViewProjection[11] * Point[2] + ViewProjection[15];
    if (!(W > 1.0e-4))
    {
        return null;
    }
    return [(X / W * 0.5 + 0.5) * Width, (0.5 - Y / W * 0.5) * Height];
}

// 📝 One tag's boxes. Measure(Text, Size, Spacing) is the width a string takes. The row's top-left sits on the anchor and is shifted by
//    translate-x-2 and -translate-y-1/2; its children are the 8 px dot, the 28 px line and the card, 8 px apart, centred on the row.
export function ResolveTagLayout(Anchor, Tag, Measure)
{
    const Spec   = HudSpecification.Tag;
    const Label  = Measure(Tag.Label.toUpperCase(), Spec.LabelSize, Spec.LabelSpacing);
    const Value  = Measure(Tag.Value, Spec.ValueSize, 0.0);
    const LabelLine = Spec.LabelSize * HudSpecification.LineHeight;
    const ValueLine = Spec.ValueSize * HudSpecification.LineHeight;
    const CardWidth  = Math.max(Label, Value) + 2.0 * Spec.PaddingX;
    const CardHeight = LabelLine + ValueLine + 2.0 * Spec.PaddingY;
    const RowLeft = Anchor[0] + Spec.ShiftX;
    const RowTop  = Anchor[1] - CardHeight * 0.5;
    const Middle  = Anchor[1];
    const DotLeft  = RowLeft;
    const LineLeft = DotLeft + Spec.Dot + Spec.Gap;
    const CardLeft = LineLeft + Spec.LineWidth + Spec.Gap;
    return {
        Dot:  { X: DotLeft, Y: Middle - Spec.Dot * 0.5, Size: Spec.Dot },
        Line: { X: LineLeft, Y: Middle - 0.5, Width: Spec.LineWidth },
        Card: { X: CardLeft, Y: RowTop, Width: CardWidth, Height: CardHeight },
        LabelMiddle: RowTop + Spec.PaddingY + LabelLine * 0.5,
        ValueMiddle: RowTop + Spec.PaddingY + LabelLine + ValueLine * 0.5,
        TextLeft: CardLeft + Spec.PaddingX
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       DRAWING
//------------------------------------------------------------------------------------------------------------------------

const Rgba = ([R, G, B], Alpha) => `rgba(${R}, ${G}, ${B}, ${Alpha})`;

function MeasureWith(Context)
{
    return (Text, Size, Spacing) =>
    {
        Context.font = `600 ${Size}px ${HudSpecification.FontFamily}`;
        let Total = 0.0;
        for (const Character of Text)
        {
            Total += Context.measureText(Character).width + Spacing;
        }
        return Total;
    };
}

// 📝 CSS letter-spacing: the spacing follows every character, the last included.
function DrawSpaced(Context, Text, X, Y, Size, Spacing)
{
    Context.font = `600 ${Size}px ${HudSpecification.FontFamily}`;
    Context.textBaseline = "middle";
    Context.textAlign = "left";
    let Cursor = X;
    for (const Character of Text)
    {
        Context.fillText(Character, Cursor, Y);
        Cursor += Context.measureText(Character).width + Spacing;
    }
}

// 📝 Appends a rounded rectangle to the current path; Begin starts a new path first.
function RoundedRectangle(Context, X, Y, Width, Height, Radius, Begin = true)
{
    if (Begin)
    {
        Context.beginPath();
    }
    Context.moveTo(X + Radius, Y);
    Context.lineTo(X + Width - Radius, Y);
    Context.arcTo(X + Width, Y, X + Width, Y + Radius, Radius);
    Context.lineTo(X + Width, Y + Height - Radius);
    Context.arcTo(X + Width, Y + Height, X + Width - Radius, Y + Height, Radius);
    Context.lineTo(X + Radius, Y + Height);
    Context.arcTo(X, Y + Height, X, Y + Height - Radius, Radius);
    Context.lineTo(X, Y + Radius);
    Context.arcTo(X, Y, X + Radius, Y, Radius);
    Context.closePath();
}

function DrawTag(Context, Layout, Seconds)
{
    const Spec    = HudSpecification.Tag;
    const Colours = HudSpecification.Colours;
    const Ping    = ResolvePing(Seconds);
    const Centre  = [Layout.Dot.X + Layout.Dot.Size * 0.5, Layout.Dot.Y + Layout.Dot.Size * 0.5];
    // the ping ring, cyan-300 at 60 %, scaled about the dot's middle and faded by the keyframes
    Context.fillStyle = Rgba(Colours.Cyan300, 0.6 * Ping.Opacity);
    Context.beginPath();
    Context.arc(Centre[0], Centre[1], Layout.Dot.Size * 0.5 * Ping.Scale, 0, Math.PI * 2.0);
    Context.fill();
    // the dot: cyan-100, ring-2 in cyan-200 at 40 %
    Context.fillStyle = Rgba(Colours.Cyan200, 0.4);
    Context.beginPath();
    Context.arc(Centre[0], Centre[1], Layout.Dot.Size * 0.5 + Spec.Ring, 0, Math.PI * 2.0);
    Context.moveTo(Centre[0] + Layout.Dot.Size * 0.5, Centre[1]);
    Context.arc(Centre[0], Centre[1], Layout.Dot.Size * 0.5, 0, Math.PI * 2.0, true);
    Context.fill("evenodd");
    Context.fillStyle = Rgba(Colours.Cyan100, 1.0);
    Context.beginPath();
    Context.arc(Centre[0], Centre[1], Layout.Dot.Size * 0.5, 0, Math.PI * 2.0);
    Context.fill();
    // the line: from cyan-200 at 80 % to cyan-200 at 0 across 28 px
    const Gradient = Context.createLinearGradient(Layout.Line.X, 0, Layout.Line.X + Layout.Line.Width, 0);
    Gradient.addColorStop(0.0, Rgba(Colours.Cyan200, 0.8));
    Gradient.addColorStop(1.0, Rgba(Colours.Cyan200, 0.0));
    Context.fillStyle = Gradient;
    Context.fillRect(Layout.Line.X, Layout.Line.Y, Layout.Line.Width, 1.0);
    // the card: black at 55 %, ring-1 in cyan-100 at 25 % outside it
    const Card = Layout.Card;
    Context.fillStyle = Rgba(Colours.Cyan100, 0.25);
    RoundedRectangle(Context, Card.X - Spec.RingWidth, Card.Y - Spec.RingWidth, Card.Width + 2.0 * Spec.RingWidth, Card.Height + 2.0 * Spec.RingWidth,
                     Spec.Radius + Spec.RingWidth);
    RoundedRectangle(Context, Card.X, Card.Y, Card.Width, Card.Height, Spec.Radius, false);
    Context.fill("evenodd");
    Context.fillStyle = Rgba(Colours.Black, 0.55);
    RoundedRectangle(Context, Card.X, Card.Y, Card.Width, Card.Height, Spec.Radius);
    Context.fill();
    Context.fillStyle = Rgba(Colours.Cyan200, 0.7);
    DrawSpaced(Context, Layout.Label, Layout.TextLeft, Layout.LabelMiddle, Spec.LabelSize, Spec.LabelSpacing);
    Context.fillStyle = Rgba(Colours.White, 0.9);
    DrawSpaced(Context, Layout.Value, Layout.TextLeft, Layout.ValueMiddle, Spec.ValueSize, 0.0);
}

function DrawFrame(Context, Width, Height, Seconds)
{
    const Spec    = HudSpecification;
    const Colours = Spec.Colours;
    const Box     = ResolveFrameBox(Width, Height);
    const Line    = Spec.BracketLine;
    const Size    = Spec.Bracket;
    // four corner brackets, border-cyan-200/50, 2 px on the two sides that meet in the corner
    Context.fillStyle = Rgba(Colours.Cyan200, 0.5);
    for (const [Left, Top] of [[Box.X, Box.Y], [Box.X + Box.Width - Size, Box.Y], [Box.X, Box.Y + Box.Height - Size], [Box.X + Box.Width - Size, Box.Y + Box.Height - Size]])
    {
        // 📝 The two borders meet on a mitre: the corner square belongs to the horizontal one here, so it is blended once.
        const AtLeft = Left === Box.X, AtTop = Top === Box.Y;
        Context.fillRect(Left, AtTop ? Top : Top + Size - Line, Size, Line);
        Context.fillRect(AtLeft ? Left : Left + Size - Line, AtTop ? Top + Line : Top, Line, Size - Line);
    }
    // the captions: 10 px semibold uppercase, tracking 0.3em, a 15 px line box
    const Caption = Spec.Caption;
    const LineBox = Caption.Size * Spec.LineHeight;
    const Measure = MeasureWith(Context);
    const Top = Box.Y - Caption.Rise;
    const TopMiddle = Top + LineBox * 0.5;
    const Pulse = ResolvePulse(Seconds);
    Context.fillStyle = Rgba(Colours.Cyan300, Pulse);
    Context.beginPath();
    Context.arc(Box.X + Caption.DotSize * 0.5, TopMiddle, Caption.DotSize * 0.5, 0, Math.PI * 2.0);
    Context.fill();
    Context.fillStyle = Rgba(Colours.Cyan200, 0.8);
    DrawSpaced(Context, "PERCEPTION SCAN · ACTIVE", Box.X + Caption.DotSize + Caption.Gap, TopMiddle, Caption.Size, Caption.Spacing);
    const Lidar = "LIDAR · 360°";
    const LidarWidth = Measure(Lidar, Caption.Size, Caption.Spacing);
    const BottomMiddle = Box.Y + Box.Height + Caption.Rise - LineBox * 0.5;
    Context.fillStyle = Rgba(Colours.Cyan200, 0.6);
    DrawSpaced(Context, Lidar, Box.X + Box.Width - LidarWidth, BottomMiddle, Caption.Size, Caption.Spacing);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RUNTIME
//------------------------------------------------------------------------------------------------------------------------

export class ScanHud
{
    // CreateCanvas(Width, Height) returns a 2D canvas: the browser's, or any host's that has fillText.
    constructor(CreateCanvas)
    {
        this.CreateCanvas = CreateCanvas;
        this.Canvas  = null;
        this.Context = null;
        this.Width   = 0;
        this.Height  = 0;
        this.Layouts = [];
    }

    // 📝 A host with no text drawing (the Node harness's stand-in canvas) leaves the HUD absent instead of failing.
    get Capable()
    {
        return Boolean(this.Context) && typeof this.Context.fillText === "function" && typeof this.Context.measureText === "function";
    }

    Resize(Width, Height)
    {
        if (this.Canvas && this.Width === Width && this.Height === Height)
        {
            return;
        }
        this.Width  = Width;
        this.Height = Height;
        this.Canvas = this.CreateCanvas(Width, Height * 2);
        this.Context = this.Canvas.getContext("2d");
    }

    // 📝 Draws both layers. Projected: the tags' anchors already in the stage's pixels (null where behind the camera), in ScanTags order.
    //    Seconds is the clock the CSS animations began on, when the scan went on.
    Draw(Seconds, Projected)
    {
        if (!this.Capable)
        {
            return false;
        }
        const Context = this.Context;
        Context.setTransform?.(1, 0, 0, 1, 0, 0);
        Context.clearRect(0, 0, this.Width, this.Height * 2);
        const Measure = MeasureWith(Context);
        this.Layouts = [];
        ScanTags.forEach((Tag, Index) =>
        {
            const Anchor = Projected[Index];
            if (!Anchor)
            {
                return;
            }
            const Layout = ResolveTagLayout(Anchor, Tag, Measure);
            Layout.Label = Tag.Label.toUpperCase();
            Layout.Value = Tag.Value;
            this.Layouts.push(Layout);
            DrawTag(Context, Layout, Seconds);
        });
        Context.save();
        Context.translate(0, this.Height);
        DrawFrame(Context, this.Width, this.Height, Seconds);
        Context.restore();
        return true;
    }
}
