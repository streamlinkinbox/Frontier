/*============================================================================================================================================
                                                        STAMPVECTORPROPERTIES.JSX
============================================================================================================================================*/
// 🧩 Stamp vector appearance, split the way the inspector is separated: the
// colour-only paint (fill/stroke colour sources, gradients) renders on the
// Base color channel card, while shape geometry — like position and size —
// is a non-PBR property and keeps its own card above the channel cards.

import React from "react";
import PatternColourControls, { PatternPropertyCard } from "./PatternColourControls.jsx";

function Numeric({ Caption, Value, Minimum, Maximum, Step = 1, OnChange })
{
    return <label>{Caption}<input aria-label={`Stamp vector ${Caption.toLowerCase()}`} type="number" min={Minimum} max={Maximum} step={Step} value={Value ?? Minimum} onChange={Event => OnChange(+Event.target.value)}/></label>;
}

export function StampVectorPaintControls({ Geometry, OnChange, Disabled, Drawing })
{
    return <fieldset className="stamp-basecolor-paint" disabled={Disabled}>
        {Drawing && <p className="chan-note">Drawing defaults — the next vector shape uses this paint.</p>}
        <div className="stamp-basecolor-group">
            <span className="stamp-basecolor-caption">Fill</span>
            <PatternColourControls Caption="Stamp fill" ColourLabel="Stamp vector fill colour" Source={Geometry.fillSource} Solid={Geometry.color} OnChange={(Source, Colour) => OnChange({ fillSource: Source, color: Colour })}/>
            <label className="stamp-vector-toggle"><input type="checkbox" role="switch" aria-label="Stamp vector fill enabled" checked={Geometry.fillEnabled !== false} onChange={Event => OnChange({ fillEnabled: Event.target.checked })}/>Fill enabled</label>
        </div>
        <div className="stamp-basecolor-group">
            <span className="stamp-basecolor-caption">Stroke</span>
            <PatternColourControls Caption="Stamp stroke" ColourLabel="Stamp vector stroke colour" Source={Geometry.strokeSource} Solid={Geometry.stroke} OnChange={(Source, Colour) => OnChange({ strokeSource: Source, stroke: Colour, strokeWidth: Geometry.strokeWidth || 1 })}/>
            <Numeric Caption="Stroke width" Value={Geometry.strokeWidth} Minimum={0} Maximum={25} Step={.1} OnChange={Value => OnChange({ strokeWidth: Value })}/>
            <label>Line cap<select aria-label="Stamp vector line cap" value={Geometry.strokeLinecap || "round"} onChange={Event => OnChange({ strokeLinecap: Event.target.value })}>{["butt", "round", "square"].map(Caption => <option key={Caption}>{Caption}</option>)}</select></label>
            <label>Line join<select aria-label="Stamp vector line join" value={Geometry.strokeLinejoin || "round"} onChange={Event => OnChange({ strokeLinejoin: Event.target.value })}>{["miter", "round", "bevel"].map(Caption => <option key={Caption}>{Caption}</option>)}</select></label>
        </div>
    </fieldset>;
}

export function StampVectorShapeControls({ Geometry, OnChange, Disabled, Drawing })
{
    if (["path", "text"].includes(Geometry.kind)) return null;
    return <fieldset className="stamp-vector-properties stamp-shape-properties" disabled={Disabled}>
        <PatternPropertyCard Caption={Drawing ? "Shape geometry · drawing defaults" : "Shape geometry"}>
            {(!Geometry.kind || Geometry.kind === "rect") && <Numeric Caption="Corner radius" Value={Geometry.cornerRadius} Minimum={0} Maximum={50} OnChange={Value => OnChange({ cornerRadius: Value })}/>}
            {(!Geometry.kind || Geometry.kind === "polygon") && <Numeric Caption="Sides" Value={Geometry.sides} Minimum={3} Maximum={32} OnChange={Value => OnChange({ sides: Value })}/>}
            {(!Geometry.kind || Geometry.kind === "star") && <><Numeric Caption="Star points" Value={Geometry.starPoints} Minimum={3} Maximum={32} OnChange={Value => OnChange({ starPoints: Value })}/><Numeric Caption="Inner radius" Value={Geometry.innerRadius} Minimum={.05} Maximum={.95} Step={.05} OnChange={Value => OnChange({ innerRadius: Value })}/></>}
            {Geometry.kind === "arc" && <><Numeric Caption="Arc start" Value={Geometry.arcStart} Minimum={-360} Maximum={360} OnChange={Value => OnChange({ arcStart: Value })}/><Numeric Caption="Arc sweep" Value={Geometry.arcSweep} Minimum={-360} Maximum={360} OnChange={Value => OnChange({ arcSweep: Value })}/></>}
        </PatternPropertyCard>
    </fieldset>;
}
