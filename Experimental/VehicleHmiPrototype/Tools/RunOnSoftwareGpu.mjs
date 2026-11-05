//============================================================================================================================================
//                                                        RUNONSOFTWAREGPU.MJS
//============================================================================================================================================
// 🧩 Runs any command with Dawn pointed at a SwiftShader Vulkan ICD, so the WGSL really EXECUTES on the CPU (the default null
//    backend only validates). SwiftShader is fetched once from the npm tarball @sparticuz/chromium (it ships swiftshader.tar.br:
//    libvk_swiftshader.so + libvulkan.so.1 + the ICD manifest) into Tools/.swiftshader/ (git-ignored). Needs registry.npmjs.org
//    the first time only. Linux x64.
//
//    node RunOnSoftwareGpu.mjs node HeadlessHarness.mjs .. 8 "?capture=1&quality=low"      (SIZE=320x180 OUT=frame.png also work)
//    node RunOnSoftwareGpu.mjs node --test GpuExecuted.test.mjs
//    node RunOnSoftwareGpu.mjs --print                                                     (prints the environment, fetches if needed)

import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import os from "node:os";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const Here = path.dirname(fileURLToPath(import.meta.url));
export const SoftwareGpuDirectory = process.env.SOFTWARE_GPU_DIR || path.join(Here, ".swiftshader");
export const SoftwareGpuPackage = process.env.SOFTWARE_GPU_PACKAGE || "@sparticuz/chromium@153.0.0";
const Package = SoftwareGpuPackage;

function ReadTarEntries(Archive)
{
    const Entries = [];
    for (let Offset = 0; Offset + 512 <= Archive.length;)
    {
        const Name = Archive.toString("utf8", Offset, Offset + 100).replace(/\0.*$/s, "");
        if (!Name) break;
        const Size = parseInt(Archive.toString("utf8", Offset + 124, Offset + 136).replace(/\0.*$/s, "").trim() || "0", 8);
        const Type = String.fromCharCode(Archive[Offset + 156] || 48);
        if (Type === "0" || Type === "\0") Entries.push({ Name: path.basename(Name), Data: Archive.subarray(Offset + 512, Offset + 512 + Size) });
        Offset += 512 + Math.ceil(Size / 512) * 512;
    }
    return Entries;
}

export function SoftwareGpuEnvironment()
{
    const Icd = path.join(SoftwareGpuDirectory, "vk_swiftshader_icd.json");
    if (!fs.existsSync(Icd) || !fs.existsSync(path.join(SoftwareGpuDirectory, "libvulkan.so.1"))) return null;
    return { VK_ICD_FILENAMES: Icd, LD_LIBRARY_PATH: [SoftwareGpuDirectory, process.env.LD_LIBRARY_PATH].filter(Boolean).join(":"), BACKEND: "vulkan" };
}

export function FetchSoftwareGpu()
{
    if (SoftwareGpuEnvironment()) return SoftwareGpuEnvironment();
    if (os.platform() !== "linux" || os.arch() !== "x64") throw new Error("SwiftShader download is only wired for linux x64");
    const Scratch = fs.mkdtempSync(path.join(os.tmpdir(), "swiftshader-"));
    const Pack = spawnSync("npm", ["pack", Package, "--silent"], { cwd: Scratch, encoding: "utf8" });
    if (Pack.status !== 0) throw new Error("npm pack failed: " + (Pack.stderr || Pack.stdout));
    const Tarball = path.join(Scratch, Pack.stdout.trim().split("\n").pop());
    spawnSync("tar", ["-xzf", Tarball, "-C", Scratch, "package/bin/swiftshader.tar.br"]);
    const Compressed = fs.readFileSync(path.join(Scratch, "package/bin/swiftshader.tar.br"));
    const Entries = ReadTarEntries(zlib.brotliDecompressSync(Compressed));
    fs.mkdirSync(SoftwareGpuDirectory, { recursive: true });
    for (const Entry of Entries)
        if (/^(libvk_swiftshader\.so|libvulkan\.so\.1|vk_swiftshader_icd\.json)$/.test(Entry.Name))
            fs.writeFileSync(path.join(SoftwareGpuDirectory, Entry.Name), Entry.Data, { mode: 0o755 });
    fs.rmSync(Scratch, { recursive: true, force: true });
    const Environment = SoftwareGpuEnvironment();
    if (!Environment) throw new Error("SwiftShader archive did not contain the expected files");
    return Environment;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
{
    const Arguments = process.argv.slice(2);
    let Environment;
    try { Environment = FetchSoftwareGpu(); }
    catch (Failure) { console.error("software GPU unavailable:", Failure.message); process.exit(Arguments[0] === "--print" ? 1 : 77); }
    if (Arguments[0] === "--print" || Arguments.length === 0) { console.log(JSON.stringify(Environment, null, 2)); process.exit(0); }
    const Run = spawnSync(Arguments[0], Arguments.slice(1), { stdio: "inherit", env: { ...process.env, ...Environment } });
    process.exit(Run.status ?? 1);
}
