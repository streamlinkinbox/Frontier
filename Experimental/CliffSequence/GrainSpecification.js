//============================================================================================================================================
//                                                           GRAINSPECIFICATION.JS
//============================================================================================================================================
// 📦 Mineral coefficients, authoring bounds and discrete grain-study recipes; no image or noise-field inputs.

export const MineralPresets={
    Sandstone:{Label:'Cemented sandstone',Colours:['#b9a18d','#b69b84','#ac927b','#97816d'],
        Fractions:[.55,.25,.14,.06],Reactivity:[.34,.58,.95,.78],Iron:[.025,.08,.16,.85],Bond:.72},
    Crystalline:{Label:'Crystalline aggregate',Colours:['#acafa8','#969b95','#737e77','#595f5a'],
        Fractions:[.35,.40,.20,.05],Reactivity:[.23,.45,.64,.70],Iron:[.02,.06,.12,.60],Bond:.92},
    Laminated:{Label:'Weak laminated material',Colours:['#b5a180','#a08468','#79634e','#6e5847'],
        Fractions:[.25,.30,.35,.10],Reactivity:[.44,.64,.96,.78],Iron:[.03,.12,.24,.75],Bond:.59}
};
export const GrainDefaults=Object.freeze({Preset:'Sandstone',Seed:42,Resolution:24,Size:.48,Layers:5,
    Rain:.75,Drying:.25,Runoff:.65,Solvent:.70,Oxidation:.65,BondStrength:.70,WeakBand:.70});
export const GrainLimits={Seed:[0,999999],Resolution:[12,36],Size:[.06,1.2],Layers:[2,8],
    Rain:[0,1],Drying:[0,1],Runoff:[0,1],Solvent:[0,1],Oxidation:[0,1],BondStrength:[.15,1],WeakBand:[0,1]};
export const PackingProperties=['Preset','Seed','Resolution','Size','Layers','BondStrength','WeakBand'];
export function ReadGrainSpecification(Input={})
{
    const Result={...GrainDefaults};
    for (const Name of Object.keys(Result)) if (Object.hasOwn(Input,Name)) Result[Name]=Input[Name];
    if (!Object.hasOwn(MineralPresets,Result.Preset)) throw new Error('Unknown mineral preset');
    for (const [Name,[Minimum,Maximum]] of Object.entries(GrainLimits))
    {
        if (!Number.isFinite(Number(Result[Name]))) throw new Error(`${Name} must be finite`);
        Result[Name]=Math.max(Minimum,Math.min(Maximum,Number(Result[Name])));
    }
    for (const Name of ['Seed','Resolution','Layers']) Result[Name]=Math.round(Result[Name]);
    return Result;
}
