//============================================================================================================================================
//                                                             GENERATIONQUEUE.JS
//============================================================================================================================================
// 📦 Cancellable browser worker with generation revisions and explicit topology rejection.

import {CliffSequence} from './FractureSequence.js';
const Sequence=new CliffSequence();
self.onmessage=Event=>
{
    const {Revision,Specification,Through}=Event.data;
    try
    {
        const Started=performance.now();
        const Result=Sequence.Generate(Specification,Stage=>self.postMessage({Revision,Progress:Stage}),Through);
        const Invalid=Result.Stages.find(Stage=>Stage.Metrics.OpenEdges || Stage.Metrics.NonmanifoldEdges || Stage.Metrics.WindingErrors || Stage.Metrics.ZeroArea || Stage.Metrics.NonmanifoldVertices || Stage.Metrics.DuplicateTriangles);
        if (Result.Stages.some(Stage=>Stage.Records.some(Record=>!Number.isFinite(Record.Volume) || Record.Volume<=0)))
            throw new Error('Non-finite or inverted solid; result rejected.');
        if (Invalid) throw new Error(`Stage ${Invalid.Number} failed topology validation; result rejected.`);
        self.postMessage({Revision,Result,Milliseconds:performance.now()-Started});
    }
    catch (Error)
    {
        self.postMessage({Revision,Error:Error.message});
    }
};
