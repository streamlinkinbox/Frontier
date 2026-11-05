//============================================================================================================================================
//                                                          WRITELEDGERHOOKS.MJS
//============================================================================================================================================
// 🧩 Records every write the host makes to the GPU — queue.writeBuffer and queue.writeTexture, by target label, offset, size and a hash of the
//    bytes — per frame, so two runs of the same scene can be compared on what the GPU was handed. Where the ledgers agree to the last frame and
//    the pictures still differ, the difference is the software GPU's; where they diverge, the first diverging write names the host state that
//    was not deterministic.
//
//    DETERMINISTIC=1 LEDGER_OUT=<file.json> HOOKS=WriteLedgerHooks.mjs node HeadlessHarness.mjs .. <frames> "<query>"
//    node WriteLedgerHooks.mjs <a.json> <b.json>        compares two ledgers

import fs from "node:fs";
import { fileURLToPath } from "node:url";

const Out = process.env.LEDGER_OUT || "";
const Writes = [];
let CurrentFrame = -1;

// 📐 FNV-1a, 32 bits.
function HashBytes(Bytes)
{
    let Hash = 0x811c9dc5;
    for (let Index = 0; Index < Bytes.length; ++Index)
    {
        Hash = Math.imul(Hash ^ Bytes[Index], 0x01000193) >>> 0;
    }
    return Hash;
}

function ViewBytes(Data, DataOffset = 0, Size)
{
    const Source = Data.buffer ?? Data;
    const Base = (Data.byteOffset ?? 0);
    const Element = Data.BYTES_PER_ELEMENT ?? 1;
    const Start = Base + DataOffset * (Data.buffer ? Element : 1);
    const Length = Size === undefined ? (Data.byteLength - DataOffset * (Data.buffer ? Element : 1)) : Size * (Data.buffer ? Element : 1);
    return new Uint8Array(Source, Start, Length);
}

// 📝 Installed at import, before the host exists, so its construction-time uploads are in the ledger as well.
if (globalThis.GPUQueue !== undefined && Out)
{
    const QueueBuffer = globalThis.GPUQueue.prototype.writeBuffer;
    globalThis.GPUQueue.prototype.writeBuffer = function (Buffer, Offset, Data, DataOffset = 0, Size)
    {
        const Bytes = ViewBytes(Data, DataOffset, Size);
        Writes.push({ Frame: CurrentFrame, Kind: "buffer", Label: Buffer.label, Offset: Offset, Bytes: Bytes.length, Hash: HashBytes(Bytes) });
        return QueueBuffer.call(this, Buffer, Offset, Data, DataOffset, Size);
    };
    const QueueTexture = globalThis.GPUQueue.prototype.writeTexture;
    globalThis.GPUQueue.prototype.writeTexture = function (Destination, Data, Layout, Extent)
    {
        const Bytes = ViewBytes(Data, 0);
        Writes.push({ Frame: CurrentFrame, Kind: "texture", Label: Destination.texture.label, Offset: 0, Bytes: Bytes.length, Hash: HashBytes(Bytes) });
        return QueueTexture.call(this, Destination, Data, Layout, Extent);
    };
}

export default {
    BeforeFrame(Host, Frame)
    {
        CurrentFrame = Frame;
    },
    Report(Host)
    {
        if (Out)
        {
            fs.writeFileSync(Out, JSON.stringify({ Frames: CurrentFrame + 1, Writes }));
            console.log(`write ledger: ${Writes.length} writes over ${CurrentFrame + 1} frames -> ${Out}`);
        }
    }
};

// 📝 Compare mode: the first writes at which two ledgers differ, per frame.
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1])
{
    const [A, B] = [process.argv[2], process.argv[3]].map((File) => JSON.parse(fs.readFileSync(File, "utf8")));
    const Key = (Write) => `${Write.Frame}|${Write.Kind}|${Write.Label}|${Write.Offset}|${Write.Bytes}`;
    const Count = Math.min(A.Writes.length, B.Writes.length);
    let Different = 0, Reported = 0;
    const ByLabel = new Map();
    for (let Index = 0; Index < Count; ++Index)
    {
        const Left = A.Writes[Index], Right = B.Writes[Index];
        if (Key(Left) !== Key(Right) || Left.Hash !== Right.Hash)
        {
            ++Different;
            const Label = Key(Left) === Key(Right) ? `${Left.Label} (same call, different bytes)` : `${Left.Label} vs ${Right.Label} (different call)`;
            ByLabel.set(Label, (ByLabel.get(Label) ?? 0) + 1);
            if (Reported++ < 12)
            {
                console.log(`write ${Index}: frame ${Left.Frame}/${Right.Frame} ${Left.Kind} '${Left.Label}' @${Left.Offset} ${Left.Bytes} B ${Left.Hash.toString(16)} vs '${Right.Label}' ${Right.Bytes} B ${Right.Hash.toString(16)}`);
            }
        }
    }
    console.log(`${A.Writes.length} and ${B.Writes.length} writes, ${Different} of the first ${Count} differ`);
    for (const [Label, Times] of [...ByLabel.entries()].sort((Left, Right) => Right[1] - Left[1]).slice(0, 15)) console.log(`  ${Times} x ${Label}`);
}
