//============================================================================================================================================
//                                                               GRAINQUEUE.JS
//============================================================================================================================================
// 📦 Dedicated grain worker; bounded weathering batches and validated polygon boundaries never run inside the viewport loop.

import {ReadGrainSpecification,PackingProperties} from './GrainSpecification.js';
import {GrainSequence,RestoreGrainRecipe} from './GrainSequence.js';
let Sequence=null;
self.onmessage=Event=>
{
    const {Revision,Command,Specification,Source,Count,Recipe}=Event.data;
    try
    {
        if (Command==='Build') Sequence=new GrainSequence(Specification,Source);
        else if (Command==='Restore') Sequence=RestoreGrainRecipe(Recipe);
        else if (Command==='Step')
        {
            if (!Sequence||!Number.isInteger(Count)||Count<1||Count>20) throw new Error('Invalid weathering batch');
            for (let Cycle=0;Cycle<Count;++Cycle) Sequence.Step(Specification);
        }
        else throw new Error('Unknown grain command');
        const PendingSpecification=Command==='Restore'&&Recipe.PendingSpecification?ReadGrainSpecification(Recipe.PendingSpecification):null;
        if (PendingSpecification&&PackingProperties.some(Name=>PendingSpecification[Name]!==Sequence.Specification[Name]))
            throw new Error('Saved pending settings cannot change the occupied grain packing');
        const Palette=Command==='Restore'?Recipe.Palette??null:null;
        if (Palette&&(!Array.isArray(Palette)||Palette.length!==4||Palette.some(Colour=>typeof Colour!=='string'||!/^#[0-9a-f]{6}$/i.test(Colour))))
            throw new Error('Invalid mineral palette');
        const Boundary=Sequence.Boundary();
        self.postMessage({Revision,Boundary,PendingSpecification,Palette,Columns:Sequence.Columns,Metrics:Sequence.Measure(),Recipe:Sequence.Recipe(),
            Specification:Sequence.Specification,Size:Sequence.Size,Depth:Sequence.Depth,Source:Sequence.Source});
    }
    catch(Error) {self.postMessage({Revision,Error:Error.message});}
};
