//====================================================================================================================================
//                                                     COMPILESOLIDARCSHADERS.MJS
//====================================================================================================================================
// 🧩 Compiles the SolidArc Slang programs to SPIR-V (slang-loader 0.0.7 WASM) into a C++ archive header + reflection

// Run:  node Experimental/SolidArc/Native/Tooling/CompileSolidArcShaders.mjs --compiler <slang-loader>/vendor/slang-wasm.js
//       npm install --prefix _ClaudeScratch/tmp/SlangCompiler --ignore-scripts slang-loader@0.0.7   (same pin as the
//       SultanAladin/Frontier surface shaders). Push-constant offsets are asserted against the C++ mirror.

import Fs from 'node:fs';
import Path from 'node:path';
import Crypto from 'node:crypto';
import Assert from 'node:assert/strict';
import { pathToFileURL, fileURLToPath } from 'node:url';

//------------------------------------------------------------------------------------------------------------------------
//                                                    SETUP
//------------------------------------------------------------------------------------------------------------------------

const Arguments    = process.argv.slice(2);
const CompilerPath = Arguments[Arguments.indexOf('--compiler') + 1];
Assert(Arguments.includes('--compiler') && CompilerPath, 'Expected --compiler <path to slang-wasm.js>');
const Workspace        = Path.resolve(Path.dirname(fileURLToPath(import.meta.url)), '../../../../UI/SolidArc');
const ShaderDirectory  = Path.join(Workspace, 'Shaders');
const ArchivePath      = Path.join(Workspace, 'Viewport/Generated/SolidArcShaderArchive.h');
const ReflectionPath   = Path.join(Workspace, 'Viewport/Generated/SolidArcShaderReflection.json');
const InitializeCompiler = (await import(pathToFileURL(Path.resolve(CompilerPath)).href)).default;
const Compiler = await InitializeCompiler();
const Global   = Compiler.createGlobalSession();
const Target   = Compiler.getCompileTargets().find(Choice => Choice.name === 'SPIRV');
Assert(Global && Target, 'The compiler must support SPIR-V');

const MountedDirectory = '/frontier-solidarc-shaders';
Compiler.FS.mkdirTree(MountedDirectory);
const Sources = [];
for (const File of Fs.readdirSync(ShaderDirectory).filter(Name => Name.endsWith('.slang')).sort())
{
    const Contents = Fs.readFileSync(Path.join(ShaderDirectory, File));
    Compiler.FS.writeFile(MountedDirectory + '/' + File, Contents);
    Sources.push({ file: 'Shaders/' + File, sha256: Crypto.createHash('sha256').update(Contents).digest('hex') });
}
// 📝 Port step S4: the shared Frontier modules (CadStroke, MatCapLookup) sit next to the SolidArc programs in the mount.
const ModuleDirectory = Path.resolve(Workspace, '../../Engine/Render/Shader');
for (const File of ['CadStroke.slang', 'MatCapLookup.slang'])
{
    const Contents = Fs.readFileSync(Path.join(ModuleDirectory, File));
    Compiler.FS.writeFile(MountedDirectory + '/' + File, Contents);
    Sources.push({ file: 'Engine/Render/Shader/' + File, sha256: Crypto.createHash('sha256').update(Contents).digest('hex') });
}
const CompilerSource = Fs.readFileSync(Path.resolve(CompilerPath));
const CompilerBinary = Fs.readFileSync(Path.join(Path.dirname(Path.resolve(CompilerPath)), 'slang-wasm.wasm'));

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPILATION
//------------------------------------------------------------------------------------------------------------------------

const ProjectionFields = [['ClipFromWorld', 0, 64], ['ViewRight', 64, 16], ['ViewUp', 80, 16], ['ViewForward', 96, 16], ['DisplayStroke', 112, 16]];
const GridFields       = [['PlaneFromClip', 0, 64], ['AlongU', 64, 16], ['AlongV', 80, 16], ['Reserved', 96, 16], ['ReservedSecond', 112, 16]];
const PlaneFields      = [['ClipFromWorld', 0, 64], ['Corner', 64, 16], ['Right', 80, 16], ['Down', 96, 16], ['Crop', 112, 16]];
const Programs = [
    ['SolidArcSurface.slang', ['SolidArcSurfaceVertex', 'SolidArcSurfaceFragment'], ProjectionFields, [['FaceStates', 0], ['Section', 2], ['Matcaps', 0, 1]]],
    ['SolidArcEdge.slang',    ['SolidArcEdgeVertex', 'SolidArcEdgeFragment', 'SolidArcEdgeOccludedFragment', 'SolidArcEdgeIdentityFragment'], ProjectionFields, [['EdgeStates', 1], ['Section', 2]]],
    ['SolidArcGrid.slang',    ['SolidArcGridVertex', 'SolidArcGridFragment'],       GridFields,       []],
    ['SolidArcImagePlane.slang', ['SolidArcImagePlaneVertex', 'SolidArcImagePlaneFragment'], PlaneFields, [['Picture', 0, 1]]],
    ['SolidArcGizmo.slang',   ['SolidArcGizmoVertex', 'SolidArcGizmoFragment'],     ProjectionFields, []],
];
const Entries = [];
for (const [File, Names, Fields, Buffers] of Programs)
{
    const Source = Fs.readFileSync(Path.join(ShaderDirectory, File), 'utf8');
    for (const Name of Names)
    {
        const Session = Global.createSession(Target.value);
        Assert(Session, JSON.stringify(Compiler.getLastError()));
        try
        {
            const Library  = Session.loadModuleFromSource(Source, File.replace('.slang', ''), MountedDirectory + '/' + File);
            Assert(Library, JSON.stringify(Compiler.getLastError()));
            const Entry    = Library.findEntryPointByName(Name);
            Assert(Entry, JSON.stringify(Compiler.getLastError()));
            const Linked   = Session.createCompositeComponentType([Library, Entry]).link();
            Assert(Linked, JSON.stringify(Compiler.getLastError()));
            const Blob     = Linked.getEntryPointCodeBlob(0, 0);
            Assert(Blob, JSON.stringify(Compiler.getLastError()));
            const Bytes    = Buffer.from(Blob);
            Assert(Bytes.length >= 20 && Bytes.readUInt32LE(0) === 0x07230203, 'Expected SPIR-V');
            const Reflection = Linked.getLayout(0).toJsonObject();
            const Push = Reflection.parameters.find(Parameter => Parameter.name === 'Projection');
            Assert.equal(Push?.binding.kind, 'pushConstantBuffer', Name + ': push constant');
            Assert.deepEqual(Push.type.elementType.fields.map(Field => [Field.name, Field.binding.offset, Field.binding.size]), Fields, Name + ': push layout');
            Assert.equal(Push.type.elementType.sizes.find(Size => Size.kind === 'uniform').value, 128, Name + ': push size');
            for (const [Resource, Index, Space = 0] of Buffers)
            {
                const Parameter = Reflection.parameters.find(Candidate => Candidate.name === Resource);
                Assert.equal(Parameter?.binding.kind, 'descriptorTableSlot', Name + ': ' + Resource);
                Assert.equal(Parameter.binding.index, Index, Name + ': ' + Resource + ' binding');
                Assert.equal(Parameter.binding.space ?? 0, Space, Name + ': ' + Resource + ' set');
            }
            // 📝 The OpEntryPoint name is what vkCreateGraphicsPipelines must receive.
            let EntryName = null;
            for (let Offset = 20; Offset < Bytes.length;)
            {
                const Instruction = Bytes.readUInt32LE(Offset);
                const Words = Instruction >>> 16;
                Assert(Words > 0 && Offset + Words * 4 <= Bytes.length, 'Malformed SPIR-V');
                if ((Instruction & 65535) === 15) EntryName = Bytes.subarray(Offset + 12, Offset + Words * 4).toString('utf8').split('\0')[0];
                Offset += Words * 4;
            }
            Assert(EntryName, Name + ': no OpEntryPoint');
            Entries.push({ name: Name, file: File, entry: EntryName, words: Bytes.length / 4, sha256: Crypto.createHash('sha256').update(Bytes).digest('hex'),
                           bytes: Bytes, reflection: Reflection });
        }
        finally
        {
            Session.delete?.();
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ARCHIVE
//------------------------------------------------------------------------------------------------------------------------

const Ruler = '='.repeat(140);
const Title = 'SOLIDARCSHADERARCHIVE.H';
const Lines = [
    '/*' + Ruler,
    ' '.repeat(Math.floor((140 - Title.length) / 2) + 2) + Title,
    Ruler + '*/',
    '// 🧩 Generated by Tooling/CompileSolidArcShaders.mjs — SPIR-V of the SolidArc Slang programs (do not edit)',
    '',
    '// Compiler: ' + Compiler.getVersionString() + ' (slang-loader 0.0.7) · js sha256 ' + Crypto.createHash('sha256').update(CompilerSource).digest('hex').slice(0, 16) +
        ' · wasm sha256 ' + Crypto.createHash('sha256').update(CompilerBinary).digest('hex').slice(0, 16),
    ...Sources.map(Source => '// Source:   ' + Source.file + ' sha256 ' + Source.sha256),
    '',
    '#pragma once',
    '',
    '#include <cstdint>',
    '',
    'namespace Frontier::SolidArc::ShaderArchive',
    '{',
    '',
];
for (const Entry of Entries)
{
    const Words = [];
    for (let Offset = 0; Offset < Entry.bytes.length; Offset += 4) Words.push('0x' + Entry.bytes.readUInt32LE(Offset).toString(16).padStart(8, '0') + 'u');
    Lines.push('// ' + Entry.file + ' · ' + Entry.name + ' · ' + Entry.words + ' words · sha256 ' + Entry.sha256);
    Lines.push('inline constexpr const char* ' + Entry.name + 'Entry = "' + Entry.entry + '";');
    Lines.push('inline constexpr uint32_t ' + Entry.name + 'Code[] =');
    Lines.push('{');
    for (let Index = 0; Index < Words.length; Index += 8) Lines.push('    ' + Words.slice(Index, Index + 8).join(', ') + ',');
    Lines.push('};');
    Lines.push('');
}
Lines.push('} // namespace Frontier::SolidArc::ShaderArchive');
Fs.mkdirSync(Path.dirname(ArchivePath), { recursive: true });
Fs.writeFileSync(ArchivePath, Lines.join('\n') + '\n');
Fs.writeFileSync(ReflectionPath, JSON.stringify({ compiler: Compiler.getVersionString(), sources: Sources,
    entries: Entries.map(Entry => ({ name: Entry.name, entry: Entry.entry, words: Entry.words, sha256: Entry.sha256, reflection: Entry.reflection })) }, null, 1) + '\n');
console.log(`compiled ${Entries.length} entries with ${Compiler.getVersionString()} → ${Path.relative(process.cwd(), ArchivePath)}`);
for (const Entry of Entries) console.log(`  ${Entry.name.padEnd(26)} ${String(Entry.words).padStart(6)} words  entry ${Entry.entry}`);
