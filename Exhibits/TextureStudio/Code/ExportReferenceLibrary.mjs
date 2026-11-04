/*==========================================================================================================================================
                                                       EXPORTREFERENCELIBRARY.MJS
==========================================================================================================================================*/
// 🧩 Retain the HTML catalogue order, complete normalized recipes and the physical coefficients supported by native material evaluation.
import Fs from 'node:fs';
import Path from 'node:path';
import Crypto from 'node:crypto';
import {materials, normalizeMaterial} from '../../../Experimental/TextureStudio/src/materials.js';
import {getRecipe, recipeControlValue} from '../../../Experimental/TextureStudio/src/materialProfiles.js';
const Destination = Path.resolve('EngineContent/MaterialArchives/TextureStudio');
Fs.mkdirSync(Destination,{recursive:true});
const EncodeReferenceText = Text => JSON.stringify(String(Text));
const EncodeTomlTerm = Term => Array.isArray(Term) ? '[' + Term.map(EncodeTomlTerm).join(', ') + ']' :
    Term && typeof Term === 'object' ? '{' + Object.entries(Term).map(([Key, Coefficient])=>EncodeReferenceText(Key)+' = '+EncodeTomlTerm(Coefficient)).join(', ') + '}' : JSON.stringify(Term);
const ConstrainRecipeCoefficient = (Coefficient, Minimum, Maximum) => Math.max(Minimum,Math.min(Maximum,Number(Coefficient)||0));
const DecodeReferenceSpectrum = Hex => [1,3,5].map(Offset=>{const Channel=parseInt(Hex.slice(Offset,Offset+2),16)/255;return Channel<=.04045?Channel/12.92:((Channel+.055)/1.055)**2.4;});
// 📝 HTML authoring identities are random UUIDs; the native export re-issues them deterministically from the
// preset id so regeneration stays byte-stable while the document structure survives verbatim.
const DeterministicIdentity = (Preset, Original) => {const Hex=Crypto.createHash('sha256').update(Preset+':'+Original).digest('hex');return Hex.slice(0,8)+'-'+Hex.slice(8,12)+'-4'+Hex.slice(13,16)+'-8'+Hex.slice(17,20)+'-'+Hex.slice(20,32);};
// 📝 Native [Authoring] schema (MaterialAuthoring.cpp): Sources ride an array of Address tables, and a coded
// colour source additionally becomes the stratum's procedural Albedo channel so the baker can execute it.
const EncodeAuthoring = (Preset, Authoring) =>
{
    const Document = structuredClone(Authoring);
    const Remapped = new Map();
    // 📝 Seed from stable roles, never the random originals, so two exporter runs agree byte for byte.
    Document.Layers.forEach((Layer, Index) => Remapped.set(Layer.Identity, DeterministicIdentity(Preset, 'layer:'+Index+':'+Layer.Model+':'+Layer.Name)));
    Document.Generators.forEach((Generator, Index) => Remapped.set(Generator.Identity, DeterministicIdentity(Preset, 'generator:'+Index+':'+Generator.Procedure+':'+Generator.Name)));
    Document.Variables.forEach((Variable, Index) => Remapped.set(Variable.Identity, DeterministicIdentity(Preset, 'variable:'+Index+':'+Variable.Name)));
    const Remap = Identity => {if (!Remapped.has(Identity)) Remapped.set(Identity, DeterministicIdentity(Preset, Identity)); return Remapped.get(Identity);};
    for (const Layer of Document.Layers) {Layer.Identity = Remap(Layer.Identity); if (Layer.Generator) Layer.Generator = Remap(Layer.Generator);}
    for (const Generator of Document.Generators) Generator.Identity = Remap(Generator.Identity);
    for (const Variable of Document.Variables) Variable.Identity = Remap(Variable.Identity);
    for (const Address of Object.keys(Document.Links)) Document.Links[Address] = Remap(Document.Links[Address]);
    const Readdress = Address => {const Match = /^field:([a-f\d-]{36}):(.+)$/.exec(Address); return Match ? 'field:'+Remap(Match[1])+':'+Match[2] : Address;};
    for (const Generator of Document.Generators) Generator.Inputs = Generator.Inputs.map(Readdress);
    Document.Inputs = Object.fromEntries(Object.entries(Document.Inputs).map(([Address, Quantity]) => [Readdress(Address), Quantity]));
    let Text = '\n[Authoring]\nVersion = '+Document.Version+'\n';
    for (const Layer of Document.Layers)
        Text += '\n[[Authoring.Layers]]\nIdentity = '+EncodeReferenceText(Layer.Identity)+'\nName = '+EncodeReferenceText(Layer.Name)+'\nModel = '+EncodeReferenceText(Layer.Model)+'\nEnabled = '+Boolean(Layer.Enabled)+'\n'+(Layer.Generator ? 'Generator = '+EncodeReferenceText(Layer.Generator)+'\n' : '')+'Channels = '+EncodeTomlTerm(Layer.Channels)+'\n';
    for (const Generator of Document.Generators)
        Text += '\n[[Authoring.Generators]]\nIdentity = '+EncodeReferenceText(Generator.Identity)+'\nName = '+EncodeReferenceText(Generator.Name)+'\nCategory = '+EncodeReferenceText(Generator.Category)+'\nProcedure = '+EncodeReferenceText(Generator.Procedure)+'\nEnabled = '+Boolean(Generator.Enabled)+'\nCoded = '+Boolean(Generator.Coded)+'\nInputs = '+EncodeTomlTerm(Generator.Inputs)+'\nOutputs = '+EncodeTomlTerm(Generator.Outputs)+'\n';
    for (const Variable of Document.Variables)
    {
        Text += '\n[[Authoring.Variables]]\nIdentity = '+EncodeReferenceText(Variable.Identity)+'\nName = '+EncodeReferenceText(Variable.Name)+'\nAddress = '+EncodeReferenceText(Variable.Address)+'\nType = '+EncodeReferenceText(Variable.Type)+'\n';
        if (Variable.Type === 'scalar') Text += 'Minimum = '+Variable.Minimum+'\nMaximum = '+Variable.Maximum+'\n';
        if (Variable.Exposure) Text += '\n[Authoring.Variables.Exposure]\nName = '+EncodeReferenceText(Variable.Exposure.Name)+'\n'+(Variable.Type === 'scalar' ? 'Minimum = '+Variable.Exposure.Minimum+'\nMaximum = '+Variable.Exposure.Maximum+'\n' : '');
    }
    if (Object.keys(Document.Links).length)
        Text += '\n[Authoring.Links]\n'+Object.entries(Document.Links).map(([Address, Identity]) => EncodeReferenceText(Address)+' = '+EncodeReferenceText(Identity)).join('\n')+'\n';
    if (Object.keys(Document.Inputs).length)
        Text += '\n[Authoring.Inputs]\n'+Object.entries(Document.Inputs).map(([Address, Quantity]) => EncodeReferenceText(Address)+' = '+EncodeTomlTerm(Quantity)).join('\n')+'\n';
    if (Object.keys(Document.Designer || {}).length)
        Text += '\n[Authoring.Designer]\n'+Object.entries(Document.Designer).map(([Address, Quantity]) => EncodeReferenceText(Address)+' = '+EncodeTomlTerm(Quantity)).join('\n')+'\n';
    for (const [Address, Source] of Object.entries(Document.Sources))
    {
        Text += '\n[[Authoring.Sources]]\nAddress = '+EncodeReferenceText(Address)+'\nMode = '+EncodeReferenceText(Source.Mode)+'\n';
        if (Source.Direct !== undefined) Text += 'Direct = '+Boolean(Source.Direct)+'\n';
        if (Source.Generator) Text += 'Generator = '+EncodeReferenceText(Remap(Source.Generator))+'\n';
        if (Source.Stops) Text += 'Stops = '+EncodeTomlTerm(Source.Stops)+'\n';
    }
    let Channel = '';
    const ColourSource = Document.Sources.color;
    const Coded = ColourSource && Document.Generators.find(Generator => Generator.Identity === Remap(ColourSource.Generator) && Generator.Coded);
    if (Coded)
    {
        Channel = '\n[[Strata.Channels]]\nName = "Albedo"\nOp = '+EncodeReferenceText(Coded.Procedure)+'\nLink = "shared"\n[Strata.Channels.Params]\n';
        for (const Address of Coded.Inputs)
        {
            const Key = Address.split(':').at(-1), Quantity = Document.Inputs[Address];
            if (Quantity !== undefined) Channel += Key+' = '+EncodeTomlTerm(Quantity)+'\n';
        }
    }
    return {Text, Channel};
};
let Catalogue = '# Generated from the HTML library; regenerate with ExportReferenceLibrary.mjs.\nFormat = 1\n';
for (const [Ordinal, Source] of materials.entries())
{
    const Recipe = normalizeMaterial(Source), Profile = getRecipe(Recipe);
    const Filename = `${Source.id}.material`;
    const Spectrum = DecodeReferenceSpectrum(Recipe.color||'#808080');
    // 📝 The flat SourceRecipe stays free of the authoring document; the stack itself is exported into the
    // .material below so the native gate reads one authoritative copy.
    const AuthoringExport = Recipe.Authoring ? EncodeAuthoring(Source.id, Recipe.Authoring) : {Text:'', Channel:''};
    let Material = `Format = 1\nName = ${EncodeReferenceText(Source.name)}\n\n[[Strata]]\nCategory = "Opaque"\nAlbedo = [${Spectrum.join(', ')}]\nRoughness = ${ConstrainRecipeCoefficient(Recipe.roughness,.045,1)}\nMetallic = ${ConstrainRecipeCoefficient(Recipe.metalness,0,1)}\nIor = ${ConstrainRecipeCoefficient(Recipe.ior||1.5,1,3)}\nCoverage = 1.0\nAnisotropy = ${ConstrainRecipeCoefficient(Recipe.anisotropy,-.9,.9)}\n` + AuthoringExport.Channel;
    if (Recipe.coat>0)
        Material += `\n[[Strata]]\nCategory = "Coating"\nAlbedo = [1.0, 1.0, 1.0]\nRoughness = ${ConstrainRecipeCoefficient(Recipe.coatRoughness,.045,1)}\nMetallic = 0.0\nIor = ${ConstrainRecipeCoefficient(Recipe.coatIor||1.5,1,3)}\nCoverage = ${ConstrainRecipeCoefficient(Recipe.coat,0,1)}\n`;
    if (Recipe.type === 0)
    {
        const Stops = Recipe.colors.map((Colour, Index) => [...DecodeReferenceSpectrum(Colour), Recipe.colorStops[Index]]);
        if (Recipe.colorMode === 'ramp') Stops.sort((First, Second) => First[3] - Second[3]);
        Material += `\n[AutomotivePaint]\nEnabled = true\nLayers = ${Recipe.flakeLayers}\nColourMode = "${Recipe.colorMode === 'ramp' ? 'Ramp' : Recipe.colorMode === 'single' ? 'Single' : 'Palette'}"\nDistribution = ${EncodeTomlTerm([Recipe.flakes, Recipe.flakeSize, Recipe.flakeScale, Recipe.depth])}\nFinish = ${EncodeTomlTerm([Recipe.flakeRoughnessMin, Recipe.flakeRoughnessMax, Recipe.flakeMetalnessMin, Recipe.flakeMetalnessMax])}\nRelief = ${EncodeTomlTerm([Recipe.flakeTilt, Recipe.flakeLayerDepth, Recipe.orangePeel, Recipe.orangePeelScale])}\nPalette = ${EncodeTomlTerm(Stops)}\n`;
    }
    Material += AuthoringExport.Text;
    Fs.writeFileSync(Path.join(Destination,Filename),Material);
    const FlatRecipe = {...Recipe};
    delete FlatRecipe.Authoring;
    Catalogue += `\n[[Materials]]\nPath = ${EncodeReferenceText(Filename)}\nName = ${EncodeReferenceText(Source.name)}\nCategory = ${EncodeReferenceText(Source.category)}\nLabel = ${EncodeReferenceText(Source.label||'')}\nDescription = ${EncodeReferenceText(Source.description||'')}\nOrdinal = ${Ordinal}\nRecipeTitle = ${EncodeReferenceText(Profile.title)}\nRecipeCaption = ${EncodeReferenceText(Profile.caption)}\nColor = ${EncodeReferenceText(Recipe.color)}\nSourceRecipe = ${EncodeTomlTerm(FlatRecipe)}\n`;
    for (const Control of Profile.controls)
    {
        Catalogue += `\n[[Materials.Controls]]\nId = ${EncodeReferenceText(Control.id)}\nCaption = ${EncodeReferenceText(Control.label)}\nGroup = ${EncodeReferenceText(Control.group)}\nPosition = ${recipeControlValue(Recipe,Control)}\nMinimum = ${Control.direct?Control.min:0}\nMaximum = ${Control.direct?Control.max:1}\nDirect = ${Boolean(Control.direct)}\nLogarithmic = ${Boolean(Control.log)}\nStep = ${Control.step || .01}\nUnit = ${EncodeReferenceText(Control.unit || "")}\n`;
        for (const Target of Control.targets || [{key:Control.key,min:Control.min,max:Control.max}])
            Catalogue += `\n[[Materials.Controls.Targets]]\nKey = ${EncodeReferenceText(Target.key)}\nMinimum = ${Target.min}\nMaximum = ${Target.max}\n`;
    }
}
Fs.writeFileSync(Path.join(Destination,'Library.toml'),Catalogue);
const Sources = ['materials.js','materialProfiles.js','catalogExpansion.js','architectureCatalog.js','granularCatalog.js','botanicalCatalog.js','MaterialSpecification.js','FieldSpecification.js'].map(Name=>{const File='Experimental/TextureStudio/src/'+Name;return{file:File,sha256:Crypto.createHash('sha256').update(Fs.readFileSync(File)).digest('hex')};});
Fs.writeFileSync('Exhibits/TextureStudio/Results/ReferenceLibraryExport.json',JSON.stringify({count:materials.length,sources:Sources,outputs:Fs.readdirSync(Destination,{withFileTypes:true}).filter(Entry=>Entry.isFile()).map(Entry=>Entry.name).sort().map(Name=>({file:'EngineContent/MaterialArchives/TextureStudio/'+Name,sha256:Crypto.createHash('sha256').update(Fs.readFileSync(Path.join(Destination,Name))).digest('hex')})),rendering_boundary:'Complete HTML normalized recipes retained; native physical coefficients and executable automotive paint parameters exported. Other procedural families are not yet ported.'},null,2)+'\n');
console.log(`Exported ${materials.length} HTML presets with complete normalized recipes and ordered metadata.`);
