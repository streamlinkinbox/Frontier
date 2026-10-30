//============================================================================================================================================
//                                                      COMPILEBAKEFIELD.MJS
//============================================================================================================================================
// 🧩 Compile and authenticate the mesh-bake field compute entry point.

import Fs from 'node:fs';
import Path from 'node:path';
import Crypto from 'node:crypto';
import {fileURLToPath, pathToFileURL} from 'node:url';
import Assert from 'node:assert/strict';

const [CompilerPath, Destination] = process.argv.slice(2);
Assert(CompilerPath && Destination, 'Expected Slang WASM entry path and archive destination');
const Compiler = await (await import(pathToFileURL(Path.resolve(CompilerPath)).href)).default();
const Global = Compiler.createGlobalSession();
const Target = Compiler.getCompileTargets().find(Choice => Choice.name === 'SPIRV');
Assert(Global && Target, 'Expected portable SPIR-V compiler');
const Root = Path.resolve(Path.dirname(fileURLToPath(import.meta.url)), '..');
const SourcePath = Path.join(Root, 'Engine/Render/Shader/BakeFieldEvaluation.slang');
const Source = Fs.readFileSync(SourcePath, 'utf8');
const Mount = '/frontier-bake-field';
Compiler.FS.mkdirTree(Mount);
Compiler.FS.writeFile(Mount + '/BakeFieldEvaluation.slang', Source);
const Session = Global.createSession(Target.value);
const Module = Session.loadModuleFromSource(Source, 'BakeFieldEvaluation', Mount + '/BakeFieldEvaluation.slang');
Assert(Module, JSON.stringify(Compiler.getLastError()));
const Entry = Module.findEntryPointByName('BakeFieldCompute');
Assert(Entry, JSON.stringify(Compiler.getLastError()));
const Linked = Session.createCompositeComponentType([Module, Entry]).link();
Assert(Linked, JSON.stringify(Compiler.getLastError()));
const Bytes = Buffer.from(Linked.getEntryPointCodeBlob(0, 0));
Assert(Bytes.length >= 20 && Bytes.readUInt32LE(0) === 0x07230203, 'Expected valid SPIR-V');
const Reflection = Linked.getLayout(0).toJsonObject();
const UniformBlock = Reflection.parameters.find(Parameter => Parameter.name === 'Uniforms');
// 📝 48 bytes is the push-constant ABI shared with Engine/ContentInterchange/BakeField.h's BakeFieldUniforms
// (4 uint32 + 8 float32); its static_assert guards the C++ side, this assertion guards the Slang side.
Assert.equal(UniformBlock?.type.elementType.sizes.find(Size => Size.kind === 'uniform').value, 48,
    'Bake field push ABI must contain 48 bytes.');
let ComputeEntry = false;
for (let Offset = 20; Offset < Bytes.length;)
{
    const Header = Bytes.readUInt32LE(Offset), Words = Header >>> 16;
    Assert(Words > 0 && Offset + Words * 4 <= Bytes.length, 'Malformed compiled bake-field SPIR-V instruction.');
    // 📝 OpEntryPoint (opcode 15) with execution model 5 (GLCompute) proves the archive is the compute entry.
    if ((Header & 65535) === 15 && Words >= 4 && Bytes.readUInt32LE(Offset + 4) === 5) ComputeEntry = true;
    Offset += Words * 4;
}
Assert(ComputeEntry, 'Bake field archive has no compute entry point.');
Fs.mkdirSync(Destination, {recursive: true});
Fs.writeFileSync(Path.join(Destination, 'BakeFieldCompute.spv'), Bytes);
const Manifest = {compiler: Compiler.getVersionString(), entry: 'BakeFieldCompute', target: 'SPIRV',
    push_constant_bytes: 48,
    source_sha256: Crypto.createHash('sha256').update(Source).digest('hex'), dependencies: [],
    binary_sha256: Crypto.createHash('sha256').update(Bytes).digest('hex')};
Fs.writeFileSync(Path.join(Destination, 'BakeFieldCompilation.json'), JSON.stringify(Manifest, null, 2) + '\n');
console.log('PASS: BakeFieldCompute compiled with a 48-byte push-constant ABI.');
