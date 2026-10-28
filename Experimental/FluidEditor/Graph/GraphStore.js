/*====================================================================================================================================
                                                        GRAPHSTORE.JS
====================================================================================================================================*/
// 🧩 Entry/link record store, token issuance, and admissibility of a proposed connection

import
{
    ResolveInboundLabels,
    CategoryPresentation,
    CompactEntryCondition
} from './CatalogueSpecifications.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                     TOKEN ISSUANCE
//------------------------------------------------------------------------------------------------------------------------

let EntryCounter = 1;
let PortCounter  = 1;

export function IssueEntryToken() { return `entry_${EntryCounter++}`; }
export function IssuePortToken()  { return `port_${PortCounter++}`; }

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD STORE
//------------------------------------------------------------------------------------------------------------------------

export const RecordStore =
{
    Entries: [],
    Links:   [],

    // Pan/zoom of the graph plane.
    PlaneOffsetX: 0,
    PlaneOffsetY: 0,
    PlaneScale:   0.75,

    CanvasImmobilized:  false,
    ControlsRevealed:   false,
    ViewportSide:       'right',   // 'right' | 'left' | 'hidden'
    SimulationRunning:  false
};

export function RetrieveEntry(EntryToken)
{
    return RecordStore.Entries.find((Entry) => Entry.Token === EntryToken) || null;
}

export function RetrieveChosenEntry()
{
    return RecordStore.Entries.find((Entry) => Entry.Chosen) || null;
}

export function RetrievePort(EntryToken, PortToken)
{
    const Entry = RetrieveEntry(EntryToken);
    if (!Entry) return null;
    return Entry.InboundPorts.find((Port) => Port.Token === PortToken)
        || Entry.OutboundPorts.find((Port) => Port.Token === PortToken)
        || null;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENTRY CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// Construct the single entry the editor opens with.
export function ConstructOriginEntry()
{
    return {
        Token:        IssueEntryToken(),
        Compact:      false,
        CatalogueToken: null,
        Category:     null,
        Title:        'Start',
        Subtitle:     'Entry Point',
        Glyph:        'Box',
        GlyphClass:   'GlyphNeutral',
        PlaneX:       350,
        PlaneY:       250,
        InboundPorts: [],
        OutboundPorts:[{ Token: IssuePortToken(), Label: 'Flow', Classification: 'any' }],
        Immobilized:  false,
        Exported:     false,
        Folded:       false,
        Tacked:       false,
        Chosen:       false,
        Annotation:   undefined,
        Notice:       undefined,
        PortScalars:  {},
        RevealedScalars: {},
        BuildCondition: 'built',
        BuildProgress:  100
    };
}

// Construct an entry from a catalogue item at the given plane coordinate.
export function ConstructCatalogueEntry(CatalogueItem, Category, PlaneX, PlaneY)
{
    const Presentation = CategoryPresentation[Category] || CategoryPresentation.utility;
    const InboundLabels = ResolveInboundLabels(CatalogueItem.Token, CatalogueItem.InboundPorts.length);

    const InboundPorts = CatalogueItem.InboundPorts.map((Classification, PortIndex) => ({
        Token:          IssuePortToken(),
        Label:          InboundLabels[PortIndex],
        Classification: Classification
    }));

    return {
        Token:          IssueEntryToken(),
        Compact:        CompactEntryCondition(CatalogueItem.Token),
        CatalogueToken: CatalogueItem.Token,
        Category:       Category,
        Title:          CatalogueItem.Naming,
        Subtitle:       CatalogueItem.Summary,
        Glyph:          Presentation.Glyph,
        GlyphClass:     Presentation.GlyphClass,
        PlaneX:         PlaneX,
        PlaneY:         PlaneY,
        InboundPorts:   InboundPorts,
        OutboundPorts:  [{ Token: IssuePortToken(), Label: 'Out', Classification: CatalogueItem.OutboundPort || 'any' }],
        Immobilized:    false,
        Exported:       false,
        Folded:         false,
        Tacked:         false,
        Chosen:         false,
        Annotation:     undefined,
        Notice:         undefined,
        PortScalars:    {},
        RevealedScalars:{},
        BuildCondition: 'built',
        BuildProgress:  100
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PORT MUTATION
//------------------------------------------------------------------------------------------------------------------------

export function AttachInboundPort(EntryToken)
{
    const Entry = RetrieveEntry(EntryToken);
    if (!Entry) return;
    Entry.InboundPorts.push({
        Token:          IssuePortToken(),
        Label:          `In ${Entry.InboundPorts.length + 1}`,
        Classification: 'any'
    });
}

export function AttachOutboundPort(EntryToken)
{
    const Entry = RetrieveEntry(EntryToken);
    if (!Entry) return;
    Entry.OutboundPorts.push({
        Token:          IssuePortToken(),
        Label:          `Out ${Entry.OutboundPorts.length + 1}`,
        Classification: 'any'
    });
}

export function DetachInboundPort(EntryToken, PortToken)
{
    const Entry = RetrieveEntry(EntryToken);
    if (!Entry) return;
    Entry.InboundPorts = Entry.InboundPorts.filter((Port) => Port.Token !== PortToken);
    RecordStore.Links = RecordStore.Links.filter(
        (Link) => !(Link.TargetEntry === EntryToken && Link.TargetPort === PortToken));
    delete Entry.PortScalars[PortToken];
    delete Entry.RevealedScalars[PortToken];
}

export function DetachOutboundPort(EntryToken, PortToken)
{
    const Entry = RetrieveEntry(EntryToken);
    if (!Entry) return;
    Entry.OutboundPorts = Entry.OutboundPorts.filter((Port) => Port.Token !== PortToken);
    RecordStore.Links = RecordStore.Links.filter(
        (Link) => !(Link.SourceEntry === EntryToken && Link.SourcePort === PortToken));
}

export function ReclaimEntry(EntryToken)
{
    RecordStore.Entries = RecordStore.Entries.filter((Entry) => Entry.Token !== EntryToken);
    RecordStore.Links = RecordStore.Links.filter(
        (Link) => Link.SourceEntry !== EntryToken && Link.TargetEntry !== EntryToken);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LINK ADMISSIBILITY
//------------------------------------------------------------------------------------------------------------------------

// 📝 A link is admissible when the two endpoints sit on different entries, oppose in direction, are not
//    already linked, and carry compatible port classifications ('any' matches everything).
export function AdmissibleLinkCondition(SourceEntry, SourcePort, TargetEntry, TargetPort)
{
    if (!SourceEntry || !TargetEntry) return false;
    if (SourceEntry === TargetEntry)  return false;

    const OriginPort = RetrievePort(SourceEntry, SourcePort);
    const ArrivalPort = RetrievePort(TargetEntry, TargetPort);
    if (!OriginPort || !ArrivalPort) return false;

    const DuplicateCondition = RecordStore.Links.some((Link) =>
        Link.SourceEntry === SourceEntry && Link.SourcePort === SourcePort &&
        Link.TargetEntry === TargetEntry && Link.TargetPort === TargetPort);
    if (DuplicateCondition) return false;

    const OriginClassification  = OriginPort.Classification  || 'any';
    const ArrivalClassification = ArrivalPort.Classification || 'any';
    if (OriginClassification === 'any' || ArrivalClassification === 'any') return true;
    return OriginClassification === ArrivalClassification;
}

export function IntegrateLink(SourceEntry, SourcePort, TargetEntry, TargetPort)
{
    if (!AdmissibleLinkCondition(SourceEntry, SourcePort, TargetEntry, TargetPort)) return false;

    // 📝 An inbound port accepts exactly one link; a new arrival replaces the previous one.
    RecordStore.Links = RecordStore.Links.filter(
        (Link) => !(Link.TargetEntry === TargetEntry && Link.TargetPort === TargetPort));

    RecordStore.Links.push({
        Token:       `link_${SourceEntry}_${SourcePort}_${TargetEntry}_${TargetPort}`,
        SourceEntry: SourceEntry,
        SourcePort:  SourcePort,
        TargetEntry: TargetEntry,
        TargetPort:  TargetPort
    });
    return true;
}

export function ReclaimLink(LinkToken)
{
    RecordStore.Links = RecordStore.Links.filter((Link) => Link.Token !== LinkToken);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BUILD CONDITION
//------------------------------------------------------------------------------------------------------------------------

const BuildTimers = new Map();

// 📝 Editing a scalar puts an entry into 'building'; it settles to 'built' 500 ms later, matching the
//    reference editor's simulated rebuild.
export function ProvokeRebuild(EntryToken, OnSettled)
{
    const Entry = RetrieveEntry(EntryToken);
    if (!Entry) return;

    Entry.BuildCondition = 'building';
    Entry.BuildProgress  = 45;

    if (BuildTimers.has(EntryToken)) clearTimeout(BuildTimers.get(EntryToken));
    BuildTimers.set(EntryToken, setTimeout(() =>
    {
        const Settling = RetrieveEntry(EntryToken);
        if (Settling)
        {
            Settling.BuildCondition = 'built';
            Settling.BuildProgress  = 100;
        }
        BuildTimers.delete(EntryToken);
        if (OnSettled) OnSettled();
    }, 500));
}
