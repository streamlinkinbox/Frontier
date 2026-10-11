//============================================================================================================================================
//                                                           CLIFFSPECIFICATION.JS
//============================================================================================================================================
// 📦 Authored geological feature catalogues and bounded polygon cliff parameters.

export const CliffDefaults = Object.freeze({Profile:'Headland', Seed:42, Width:32, Height:18, Depth:12, Relief:1,
    NoiseMode:'Ridged', Variation:.8, NoiseScale:2.6, FractureStyle:'Conjugate', FractureSeed:42,
    Retreat:0.48, Beds:7, Dip:4, Aperture:0.07, FractureBend:1, JointSpacing:4.6, Penetration:0.78, FaceRecess:0.65, SpallSize:0.8,
    SpallDensity:0.8, CrackLength:1.4, CrackWidth:0.12, CrackDepth:0.18, CrackDensity:0.55, TriangleSpan:1.4});

// 📝 These are authored geological cross-sections, not samples of a displacement function.
// 📝 Each station is [horizontal extent, crown elevation, promontory projection].
export const CliffProfiles = Object.freeze(
{
    Headland:
    {
        Label:'Buttressed headland',
        Stations:[[0,.67,-.06],[.09,.82,.08],[.20,.87,.18],[.29,.76,-.20],[.38,.83,-.28],
            [.47,.97,.12],[.61,1,.20],[.70,.88,-.10],[.78,.77,-.31],[.88,.89,.10],[1,.70,-.05]],
        Sections:[[0,0],[.15,-.13],[.46,-.17],[.76,-.38],[1,-.32]],
        SectionKinds:[0,1,1,2,2,3,3,2,2,1,0]
    },
    Escarpment:
    {
        Label:'Stepped escarpment',
        Stations:[[0,.60,-.18],[.12,.68,.03],[.25,.70,.09],[.32,.87,-.13],[.45,.90,.04],
            [.59,1,.09],[.72,.94,-.16],[.81,.78,-.26],[.91,.77,.03],[1,.62,-.12]],
        Sections:[[0,0],[.21,-.09],[.45,-.29],[.73,-.31],[1,-.42]],
        SectionKinds:[0,3,3,2,1,1,2,2,3,0]
    },
    Spire:
    {
        Label:'Tall solitary spire',
        Stations:[[0,.26,-.10],[.14,.34,.02],[.28,.48,.07],[.40,.88,.12],[.48,1,.20],
            [.58,.94,.10],[.70,.49,-.10],[.84,.34,.05],[1,.24,-.10]],
        Sections:[[0,0],[.18,0],[.44,0],[.74,0],[1,0]],
        SectionKinds:[0,1,1,3,3,2,2,1,0], Taper:[1,.96,.89,.78,.65]
    },
    Needles:
    {
        Label:'Serrated needle ridge',
        Stations:[[0,.26,-.06],[.10,.78,.16],[.20,.91,.22],[.29,.31,-.25],[.40,.87,.12],
            [.48,1,.22],[.59,.34,-.30],[.70,.92,.21],[.78,.84,.12],[.88,.32,-.22],[1,.56,.05]],
        Sections:[[0,0],[.15,0],[.42,0],[.73,0],[1,0]],
        SectionKinds:[0,1,3,2,1,3,2,3,1,2,0], Taper:[1,.98,.92,.84,.76]
    },
    WideWall:
    {
        Label:'Broad broken escarpment',
        Stations:[[0,.70,-.10],[.07,.79,.10],[.16,.92,.14],[.24,.88,-.12],[.33,.91,-.17],
            [.42,.86,.09],[.51,1,.16],[.61,.88,-.13],[.70,.86,-.23],[.79,.93,.14],[.9,.85,.08],[1,.72,-.12]],
        Sections:[[0,0],[.17,0],[.45,0],[.75,0],[1,0]],
        SectionKinds:[0,1,3,2,2,1,3,2,2,3,1,0]
    },
    Amphitheatre:
    {
        Label:'Recessed amphitheatre',
        Stations:[[0,.64,.10],[.12,.91,.18],[.23,1,.08],[.32,.86,-.18],[.44,.80,-.38],
            [.56,.82,-.39],[.67,.86,-.24],[.77,.96,.09],[.90,.89,.16],[1,.61,.02]],
        Sections:[[0,0],[.18,-.16],[.49,-.22],[.77,-.40],[1,-.34]],
        SectionKinds:[0,1,1,2,2,2,2,3,3,0]
    }
});

export const NoiseModes={None:'None · authored profiles',Gradient:'Layered gradient',Ridged:'Ridged gradient',Cellular:'Cellular ridges'};
export const FractureStyles={Conjugate:'Conjugate fractures',Orthogonal:'Block-jointed rock',Vertical:'Steep joint set',Bedding:'Dipping bedding'};
export const FormationPresets={
    Headland:{Width:32,Height:18,Depth:12,Relief:1,Retreat:.48,NoiseMode:'Ridged',Variation:.8,NoiseScale:2.6},
    Escarpment:{Width:42,Height:22,Depth:14,Relief:.85,Retreat:.42,NoiseMode:'Gradient',Variation:.7,NoiseScale:2.8},
    Amphitheatre:{Width:36,Height:22,Depth:16,Relief:1,Retreat:.45,NoiseMode:'Cellular',Variation:.7,NoiseScale:2.4},
    Spire:{Width:16,Height:44,Depth:14,Relief:.7,Retreat:.42,NoiseMode:'Ridged',Variation:.65,NoiseScale:2.2},
    Needles:{Width:36,Height:42,Depth:13,Relief:1,Retreat:.44,NoiseMode:'Ridged',Variation:.75,NoiseScale:3.2},
    WideWall:{Width:64,Height:22,Depth:16,Relief:.85,Retreat:.4,NoiseMode:'Gradient',Variation:.9,NoiseScale:3.4}};

export const SectionPlans = [[0,-.08,-.23,-.29,-.43],[0,-.035,-.09,-.21,-.43],
    [-.06,-.22,-.25,-.34,-.47],[0,-.13,-.16,-.38,-.33]];
export const RecessPlans = [{Normal:[.16,.04,1],Depth:.8},{Normal:[-.19,-.06,1],Depth:.45},
    {Normal:[.08,.12,1],Depth:1.1},{Normal:[-.13,.08,1],Depth:.65}];

export const BeddingPlans = [[1.15,.8,1.1,.85,1.3,.75,1.05],[.85,1.25,.85,1.15,.8,1.1,1.05],
    [1.25,.9,.75,1.2,1,.9,1.1]];
export const JointPlans = [
    {Lean:.10, Obliquity:.13, Stagger:[0,.26,-.18,.12]},
    {Lean:-.08, Obliquity:-.19, Stagger:[.19,-.12,.28,0]},
    {Lean:.16, Obliquity:-.08, Stagger:[-.14,.21,0,.12]}];
export const SpallPlans = [
    {Position:.40, Length:.85, FirstDepth:.72, SecondDepth:.46, Shear:-.12, Root:.32, Shape:'Flake'},
    {Position:.62, Length:1.25, FirstDepth:.48, SecondDepth:.82, Shear:.19, Root:.24, Shape:'Flake'},
    {Position:.48, Length:1.00, FirstDepth:.85, SecondDepth:.61, Shear:.10, Root:.38, Shape:'Wedge'},
    {Position:.30, Length:.70, FirstDepth:.57, SecondDepth:.73, Shear:-.16, Root:.30, Shape:'Flake'}];
export const CrackPlans = [
    {Tilt:.18, Offset:.12, Path:[[-.5,0],[-.17,.08],[.15,.04],[.5,.10]]},
    {Tilt:-.30, Offset:-.10, Path:[[-.5,.05],[-.12,0],[.22,.09],[.5,.03]]},
    {Tilt:.44, Offset:0, Path:[[-.5,0],[-.2,-.07],[.18,.03],[.5,0]]}];

// 📝 Deterministic catalogue selection; coherent formation noise uses the independent relief sampler.
export function SelectCatalogue(Seed)
{
    let State = Number(Seed) >>> 0;
    return Count =>
    {
        State = (Math.imul(State, 1664525) + 1013904223) >>> 0;
        return Math.floor(State / 4294967296 * Count);
    };
}

export function ReadSpecification(Input = {})
{
    const Result = {...CliffDefaults, ...Input};
    const Limits = {Seed:[0,999999],FractureSeed:[0,999999],Variation:[0,1],NoiseScale:[1,5],Width:[10,80],Height:[10,56],Depth:[8,24],Relief:[.35,1.3],Retreat:[.25,.65],
        Beds:[4,10],Dip:[-8,8],Aperture:[.035,.18],FractureBend:[0,1.5],JointSpacing:[3,7],Penetration:[.55,.9],FaceRecess:[0,1.5],SpallSize:[.25,1.3],
        SpallDensity:[0,1],CrackLength:[.5,2.2],CrackWidth:[.07,.22],CrackDepth:[.06,.3],CrackDensity:[0,1],TriangleSpan:[.8,2.2]};
    if (!Object.hasOwn(CliffProfiles,Result.Profile)) throw new Error('Unknown cliff profile');
    if (!Object.hasOwn(NoiseModes,Result.NoiseMode)) throw new Error('Unknown noise mode');
    if (!Object.hasOwn(FractureStyles,Result.FractureStyle)) throw new Error('Unknown fracture style');
    for (const [Name, [Minimum, Maximum]] of Object.entries(Limits))
    {
        if (!Number.isFinite(Number(Result[Name]))) throw new Error(`${Name} must be finite`);
        Result[Name] = Math.max(Minimum, Math.min(Maximum, Number(Result[Name])));
    }
    Result.Seed = Math.round(Result.Seed);
    Result.Beds = Math.round(Result.Beds);
    Result.FractureSeed = Math.round(Result.FractureSeed);
    return Result;
}

export const StageProperties=[
    ['Profile','Seed','NoiseMode','Variation','NoiseScale','Width','Height','Depth','Relief','Retreat','TriangleSpan'],
    ['FractureSeed','FractureStyle','Beds','Dip','Aperture','FractureBend'],
    ['JointSpacing','Penetration','FaceRecess'],
    ['SpallSize','SpallDensity'],
    ['CrackLength','CrackWidth','CrackDepth','CrackDensity']];

export function EarliestStage(Before,After)
{
    if (!Before) return 1;
    const Index=StageProperties.findIndex(Names=>Names.some(Name=>Before[Name]!==After[Name]));
    return Index<0?6:Index+1;
}

export function ReadRecipe(Recipe)
{
    if (Recipe.Format!=='Frontier.PolygonCliff' || ![1,2].includes(Recipe.Version)) throw new Error('Unsupported cliff recipe');
    const Specification={...Recipe.Specification};
    if (Recipe.Version===1)
    {
        Object.assign(Specification,{NoiseMode:'None',Variation:0,FractureStyle:'Bedding',FractureSeed:Specification.Seed??42});
    }
    return ReadSpecification(Specification);
}
