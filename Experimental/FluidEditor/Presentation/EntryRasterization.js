/*====================================================================================================================================
                                                    ENTRYRASTERIZATION.JS
====================================================================================================================================*/
// 🧩 Builds the DOM for one graph entry — full-form node, circular operator, and input regulator

import { ComposeGlyph, TrigonometricCurves } from './GlyphOutlines.js';
import
{
    ResolvePortDotClass, OperatorTokens, AxisTokens, SweepTokens
} from '../Graph/CatalogueSpecifications.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                     HOVER TOOLBAR
//------------------------------------------------------------------------------------------------------------------------

function ComposeToolbarAction(ActionNaming, Hint, OutlineNaming, EngagedClass)
{
    return `<div class="ActionHint" data-hint="${Hint}">`
         +   `<button class="ToolbarAction ${EngagedClass || ''}" data-action="${ActionNaming}">`
         +     ComposeGlyph(OutlineNaming, 14)
         +   `</button>`
         + `</div>`;
}

function ComposeFullToolbar(Entry)
{
    const FoldHint  = Entry.Folded ? 'Expand' : 'Collapse';
    const FoldGlyph = Entry.Folded ? 'Expand' : 'Contract';
    const LockHint  = Entry.Immobilized ? 'Unlock Node' : 'Lock Node';
    const LockGlyph = Entry.Immobilized ? 'Fastened' : 'Unfastened';

    return `<div class="EntryToolbar">`
         +   ComposeToolbarAction('Fold', FoldHint, FoldGlyph, Entry.Folded ? 'Engaged' : '')
         +   `<div class="ToolbarRule"></div>`
         +   ComposeToolbarAction('Preview', 'Preview', 'Observe')
         +   ComposeToolbarAction('Tack', Entry.Tacked ? 'Unpin' : 'Pin', 'Tack', Entry.Tacked ? 'EngagedIndigo' : '')
         +   `<div class="ToolbarRule"></div>`
         +   ComposeToolbarAction('Immobilize', LockHint, LockGlyph, Entry.Immobilized ? 'EngagedAmber' : '')
         +   ComposeToolbarAction('Annotate', 'Comment', 'Annotation')
         +   ComposeToolbarAction('Notice', 'Set Tooltip', 'Notice')
         +   `<div class="ToolbarRule"></div>`
         +   ComposeToolbarAction('Reclaim', 'Delete', 'Trash', 'Destructive')
         + `</div>`;
}

function ComposeCompactToolbar()
{
    return `<div class="EntryToolbar">`
         +   ComposeToolbarAction('Preview', 'Preview', 'Observe')
         +   ComposeToolbarAction('Annotate', 'Comment', 'Annotation')
         +   `<div class="ToolbarRule"></div>`
         +   ComposeToolbarAction('Reclaim', 'Delete', 'Trash', 'Destructive')
         + `</div>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHARED DECORATIONS
//------------------------------------------------------------------------------------------------------------------------

function ComposeExportBadge(Entry, CompactCondition)
{
    if (!Entry.Exported) return '';
    const BadgeClass = CompactCondition ? 'ExportBadge Compact' : 'ExportBadge';
    const BadgeText  = CompactCondition ? 'Exp' : 'Export';
    return `<button class="${BadgeClass}" data-action="Export">${BadgeText}</button>`;
}

function ComposeNotice(Entry)
{
    if (!Entry.Notice) return '';
    return `<div class="EntryTooltip">${EscapeMarkup(Entry.Notice)}</div>`;
}

function ComposeAnnotationPane(Entry, CompactCondition)
{
    if (Entry.Annotation === undefined && !Entry.Composing) return '';

    const PaneClass = CompactCondition ? 'CommentPane Compact' : 'CommentPane';
    if (Entry.Composing)
    {
        return `<div class="${PaneClass} Composing">`
             +   `<textarea class="CommentField" data-action="AnnotationField" rows="2" `
             +   `placeholder="Add a comment...">${EscapeMarkup(Entry.Annotation || '')}</textarea>`
             + `</div>`;
    }

    const AnnotationBody = Entry.Annotation
        ? EscapeMarkup(Entry.Annotation)
        : `<span class="Vacant">Empty comment...</span>`;
    return `<div class="${PaneClass}">`
         +   `<div class="CommentBody" data-action="AnnotationOpen">${AnnotationBody}</div>`
         + `</div>`;
}

export function EscapeMarkup(RawText)
{
    return String(RawText)
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FULL-FORM ENTRY
//------------------------------------------------------------------------------------------------------------------------

function ComposeInboundRow(Entry, Port, PortIndex)
{
    const DotClass = ResolvePortDotClass(Port.Classification);
    const ScalarRevealed = Entry.RevealedScalars[Port.Token];

    const Affordance = ScalarRevealed
        ? `<input class="PinScalarField" type="text" data-action="PortScalar" data-port="${Port.Token}" `
        + `value="${EscapeMarkup(Entry.PortScalars[Port.Token] || '')}" placeholder="0.0" `
        + `title="Double click to remove value">`
        : `<button class="PinAffordance" data-action="PortScalarReveal" data-port="${Port.Token}" title="Set Value">`
        + ComposeGlyph('Minus', 10) + `</button>`;

    return `<div class="PinRow">`
         +   `<div class="PinDot Inbound ${DotClass}" data-porthit="target" `
         +   `data-entry="${Entry.Token}" data-port="${Port.Token}"></div>`
         +   `<span class="PinLabel">${EscapeMarkup(Port.Label || `In ${PortIndex + 1}`)}</span>`
         +   Affordance
         + `</div>`;
}

function ComposeOutboundRow(Entry, Port, PortIndex)
{
    const DotClass = ResolvePortDotClass(Port.Classification);
    return `<div class="PinRow Outbound">`
         +   `<button class="PinAffordance Severing" data-action="DetachOutbound" data-port="${Port.Token}" `
         +   `title="Remove Pin">` + ComposeGlyph('Minus', 10) + `</button>`
         +   `<span class="PinLabel">${EscapeMarkup(Port.Label || `Out ${PortIndex + 1}`)}</span>`
         +   `<div class="PinDot Outbound ${DotClass}" data-porthit="source" `
         +   `data-entry="${Entry.Token}" data-port="${Port.Token}"></div>`
         + `</div>`;
}

function ComposeFullEntry(Entry)
{
    const InboundRows  = Entry.InboundPorts.map((Port, Index) => ComposeInboundRow(Entry, Port, Index)).join('');
    const OutboundRows = Entry.OutboundPorts.map((Port, Index) => ComposeOutboundRow(Entry, Port, Index)).join('');

    const ImmobilizedMark = Entry.Immobilized
        ? `<span style="color:rgba(234,179,8,.7);display:flex">${ComposeGlyph('Fastened', 12, 2.5)}</span>`
        : '';

    return `<div class="EntryBody" data-action="EntryBody">`
         +   ComposeExportBadge(Entry, false)
         +   ComposeFullToolbar(Entry)
         +   ComposeNotice(Entry)
         +   `<div class="EntryHead">`
         +     `<div class="HeadNaming">`
         +       `<div class="HeadGlyph ${Entry.GlyphClass}">${ComposeGlyph(Entry.Glyph, 16)}</div>`
         +       `<div class="HeadText">`
         +         `<span class="HeadTitle">${EscapeMarkup(Entry.Title)}</span>`
         +         (Entry.Subtitle ? `<span class="HeadSubtitle">${EscapeMarkup(Entry.Subtitle)}</span>` : '')
         +       `</div>`
         +     `</div>`
         +     `<div style="display:flex;align-items:center;gap:8px">${ImmobilizedMark}</div>`
         +   `</div>`
         +   `<div class="PinRegion">`
         +     `<div class="PinColumn">`
         +       `<div class="PinCaption"><span>In</span>`
         +         `<button class="CaptionAdd" data-action="AttachInbound" title="Add Input Pin">`
         +           ComposeGlyph('Plus', 12) + `</button>`
         +       `</div>`
         +       InboundRows
         +     `</div>`
         +     `<div class="PinColumn Outbound">`
         +       `<div class="PinCaption">`
         +         `<button class="CaptionAdd" data-action="AttachOutbound" title="Add Output Pin">`
         +           ComposeGlyph('Plus', 12) + `</button>`
         +         `<span>Out</span>`
         +       `</div>`
         +       OutboundRows
         +     `</div>`
         +   `</div>`
         +   ComposeAnnotationPane(Entry, false)
         + `</div>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 COMPACT (MATH) ENTRY
//------------------------------------------------------------------------------------------------------------------------

// The centred symbol of a circular or pill-shaped math operator.
function ComposeOperatorGlyph(CatalogueToken)
{
    switch (CatalogueToken)
    {
        case 'math_add':   return ComposeGlyph('Plus', 24);
        case 'math_sub':   return ComposeGlyph('Minus', 24);
        case 'math_mult':  return `<span class="Symbol">&times;</span>`;
        case 'math_div':   return `<span class="Symbol">&divide;</span>`;
        case 'math_log':   return `<span class="Word">log</span>`;
        case 'math_clamp': return `<span class="WordWide">clamp</span>`;
        case 'math_sine':  return TrigonometricCurves.Sine;
        case 'math_cos':   return TrigonometricCurves.Cosine;
        default:           return '';
    }
}

function ComposeAxisFields(Entry)
{
    const AxisSpecifications = [
        { Axis: 'X', Class: 'AxisX' },
        { Axis: 'Y', Class: 'AxisY' }
    ];
    if (Entry.CatalogueToken === 'input_vec3') AxisSpecifications.push({ Axis: 'Z', Class: 'AxisZ' });

    const Fields = AxisSpecifications.map((Specification) =>
    {
        const AxisKey = Specification.Axis.toLowerCase();
        return `<input class="AxisField ${Specification.Class}" type="text" data-action="AxisScalar" `
             + `data-axis="${AxisKey}" placeholder="${Specification.Axis}" `
             + `value="${EscapeMarkup(Entry.PortScalars[AxisKey] || '')}">`;
    }).join('');

    return `<div class="AxisRow">${Fields}</div>`;
}

function ComposeRadialDial(Entry)
{
    const DialAngle = parseFloat(Entry.PortScalars['val'] || '0') || 0;
    return `<div class="RadialDial" data-action="RadialDial">`
         +   `<div class="DialPivot"></div>`
         +   `<div class="DialNeedle" style="transform:translate(0,-50%) rotate(${DialAngle}deg)"></div>`
         + `</div>`;
}

function ComposeRegulatorCore(Entry)
{
    if (AxisTokens.includes(Entry.CatalogueToken))
    {
        return ComposeAxisFields(Entry);
    }

    const ScalarField = `<input class="RegulatorField" type="text" data-action="RegulatorScalar" `
                      + `placeholder="0.0" value="${EscapeMarkup(Entry.PortScalars['val'] || '')}">`;

    let Auxiliary = '';
    if (SweepTokens.includes(Entry.CatalogueToken) && Entry.CatalogueToken === 'input_slider')
    {
        const SweepPosition = parseFloat(Entry.PortScalars['val'] || '0') || 0;
        Auxiliary = `<input class="SweepField" type="range" min="0" max="10" step="0.1" `
                  + `data-action="RegulatorSweep" value="${SweepPosition}">`;
    }
    else if (Entry.CatalogueToken === 'input_radial')
    {
        Auxiliary = ComposeRadialDial(Entry);
    }

    return `<div style="display:flex;flex-direction:column;width:100%">${ScalarField}${Auxiliary}</div>`;
}

function ComposeCompactEntry(Entry)
{
    const OperatorCondition  = OperatorTokens.includes(Entry.CatalogueToken);
    const RegulatorCondition = AxisTokens.includes(Entry.CatalogueToken)
                            || SweepTokens.includes(Entry.CatalogueToken)
                            || Entry.CatalogueToken === 'input_radial';

    let BodyClass = 'MathBody Compound';
    if (OperatorCondition)       BodyClass = 'MathBody Operator';
    else if (RegulatorCondition) BodyClass = 'MathBody Regulator';

    let CoreMarkup;
    if (RegulatorCondition)
    {
        CoreMarkup = `<div class="RegulatorStack">`
                   +   `<span class="RegulatorCaption">${EscapeMarkup(Entry.Title)}</span>`
                   +   ComposeRegulatorCore(Entry)
                   + `</div>`;
    }
    else
    {
        const WaveCaption = ['math_sine', 'math_cos'].includes(Entry.CatalogueToken)
            ? `<span class="WaveCaption">${Entry.CatalogueToken.replace('math_', '')}</span>`
            : '';
        CoreMarkup = `<div class="OperatorGlyph">${ComposeOperatorGlyph(Entry.CatalogueToken)}</div>${WaveCaption}`;
    }

    const InboundDots = Entry.InboundPorts.map((Port) =>
        `<div class="PinDot ${ResolvePortDotClass(Port.Classification)}" data-porthit="target" `
      + `data-entry="${Entry.Token}" data-port="${Port.Token}" title="${EscapeMarkup(Port.Label || '')}"></div>`).join('');

    const OutboundDots = Entry.OutboundPorts.map((Port) =>
        `<div class="PinDot ${ResolvePortDotClass(Port.Classification)}" data-porthit="source" `
      + `data-entry="${Entry.Token}" data-port="${Port.Token}" title="${EscapeMarkup(Port.Label || '')}"></div>`).join('');

    return `<div class="${BodyClass}" data-action="EntryBody">`
         +   ComposeExportBadge(Entry, true)
         +   ComposeCompactToolbar()
         +   ComposeNotice(Entry)
         +   `<div style="display:flex;flex-direction:column;align-items:center;justify-content:center;width:100%">`
         +     CoreMarkup
         +   `</div>`
         +   `<div class="MathPinColumn Inbound">${InboundDots}</div>`
         +   `<div class="MathPinColumn Outbound">${OutboundDots}</div>`
         +   ComposeAnnotationPane(Entry, true)
         + `</div>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Rasterize one entry into a positioned wrapper element.
export function RasterizeEntry(Entry)
{
    const Wrapper = document.createElement('div');
    Wrapper.className = 'NodeEntry';
    Wrapper.dataset.entry = Entry.Token;
    Wrapper.style.transform = `translate(${Entry.PlaneX}px, ${Entry.PlaneY}px)`;

    if (Entry.Chosen)      Wrapper.classList.add('Chosen');
    if (Entry.Immobilized) Wrapper.classList.add('Immobile');
    if (Entry.Folded)      Wrapper.classList.add('Folded');

    Wrapper.innerHTML = Entry.Compact ? ComposeCompactEntry(Entry) : ComposeFullEntry(Entry);
    return Wrapper;
}
