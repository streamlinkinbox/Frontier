# 🧩 Audit deferred-suffix fresh reservoirs,conditional variance,query attribution and GPU image evidence.
from pathlib import Path
import gzip
import hashlib
import math
import re
import struct
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Images = Root / 'Exhibits/LightCompute/Images'
Prefix = 'DiffuseReservoirMis'
Count = 131072
Samples = 32768
Pairs = 16384
Methods = ('facet-uniform', 'area-proportional', 'cosine-BSDF', 'balance-MIS')
Rows = []
HashCounts = []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'{Prefix}-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Semantic = [L for L in Text.splitlines() if L.startswith('DIFFUSE_RESERVOIR_MIS_')]
    assert len(Semantic) == 5 and 'correctness=1 checks=393216' in Semantic[-1]
    assert 'reservoirs=196608' in Semantic[-1]
    Rows.append(Semantic)
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Hashes:
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Hashes))
assert Rows[0] == Rows[1]
for Line in (Results / 'DiffuseMis-evidence-hashes.txt').read_text().splitlines():
    Digest, Name = Line.split('  ', 1)
    assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
for Case in range(3):
    Experimental = {(P + 0x3b4a51d7) ^ (Case * 0x6c8e9cf5 & 0xffffffff) for P in range(Samples)}
    Previous = {(P + 0x12f7c413) ^ (Case * 0x6c8e9cf5 & 0xffffffff) for P in range(Samples)}
    assert not Experimental.intersection(Previous)
    Banks = []
    for Bank in range(2):
        Seeds = {((P + 0x5e65b9c7) ^ ((Bank * 8 + Batch) * 0x9e3779b9 & 0xffffffff) ^
                  (Case * 0x6c8e9cf5 & 0xffffffff)) for P in range(Count) for Batch in range(8)}
        assert len(Seeds) == Count * 8 and not Seeds.intersection(Experimental)
        Banks.append(Seeds)
    assert not Banks[0].intersection(Banks[1])
for Stem in ('DiffuseGenerate', 'DiffuseResolve'):
    Files = [Root / f'_ClaudeScratch/build/diffuse-reservoir-mis-{M}/{Stem}.spv'
             for M in ('release', 'sanitized')]
    assert Files[0].read_bytes() == Files[1].read_bytes()
    (Results / f'{Prefix}-{Stem}.spv.gz').write_bytes(gzip.compress(Files[0].read_bytes(), mtime=0))

def Float4(Raw, Slab, Pixel):
    return struct.unpack_from('<4f', Raw, (Slab * Count + Pixel) * 16)

def Integer(Raw, Slab, Pixel, Channel=0):
    return struct.unpack_from('<I', Raw, (Slab * Count + Pixel) * 16 + Channel * 4)[0]

def Estimate(Raw, Method, Slab):
    return [[sum(Float4(Raw, Slab, Method * Samples + Run * 1024 + P)[C]
                 for Run in range(16)) / 16 for C in range(3)] for P in range(1024)]

def MeanGate(Estimate, Reference):
    Measurements = []
    for C in range(3):
        Residuals = [E[C] - R[C] for E, R in zip(Estimate, Reference)]
        Mean = sum(Residuals) / 1024
        SE = math.sqrt(sum((R - Mean) ** 2 for R in Residuals) / (1023 * 1024))
        Measurements.append((Mean, SE, abs(Mean) <= 6 * SE + 2e-5))
    return all(M[2] for M in Measurements), Measurements

def Encode(Pixels, Name, Scale):
    Encoded = []
    for P in Pixels:
        assert all(math.isfinite(C) and C >= 0 for C in P)
        Encoded.append(tuple(round(255 * min(1, C * Scale)) for C in P))
    Tile = Image.new('RGB', (32, 32))
    Tile.putdata(Encoded)
    Tile.save(Images / Name)
    assert Image.open(Images / Name).tobytes() == bytes(C for P in Encoded for C in P)
    return Tile

Table = ['|Layout|Proposal|Arm|RMSE|Cross MSE|Calls/pixel|', '|---:|---|---|---:|---:|---:|']
Means = ['|Layout|Proposal|Arm|Channel|Residual|SE|Gate|', '|---:|---|---|---:|---:|---:|---|']
Contrasts = ['|Layout|Proposal|RIS minus raw2 cross-MSE|Tile SE|Approx95% interval|Added conditional image MSE|',
             '|---:|---|---:|---:|---|---:|']
Metrics = []
Costs = []
QualityGates = [[] for M in Methods]
CostGates = [[] for M in Methods]
NegativeFailures = [0, 0, 0, 0]
MaximumConditionalError = 0.
TotalSaved = 0
Font = ImageFont.load_default(size=14)
Small = ImageFont.load_default(size=12)
Sheets = []
for Case in range(3):
    Files = [Root / f'_ClaudeScratch/diffuse-reservoir-mis-capture-{M}/{Prefix}{Case}.f32'
             for M in ('release', 'sanitized')]
    Raw = Files[0].read_bytes()
    assert Raw == Files[1].read_bytes() and len(Raw) == 23 * Count * 16
    (Results / f'{Prefix}{Case}.f32.gz').write_bytes(gzip.compress(Raw, mtime=0))
    ReferenceRaw = gzip.decompress((Results / f'DiffuseMisReference{Case}.f32.gz').read_bytes())
    Banks = [[struct.unpack_from('<3f', ReferenceRaw, (B * 1024 + P) * 16) for P in range(1024)]
             for B in range(2)]
    Reference = [[(Banks[0][P][C] + Banks[1][P][C]) / 2 for C in range(3)] for P in range(1024)]
    ReferenceTile = Encode(Reference, f'{Prefix}-reference-{Case}.png', 64)
    Canvas = Image.new('RGB', (1120, 900), '#121923')
    Errors = Image.new('RGB', (1120, 900), '#121923')
    Pen, ErrorPen = ImageDraw.Draw(Canvas), ImageDraw.Draw(Errors)
    Pen.text((16, 12), f'Actual fresh reservoirs /layout {Case} /32x32,nearest enlarged /linear exposure64', font=Font, fill='white')
    ErrorPen.text((16, 12), f'Absolute linear RGB error /layout {Case} /scale1024,clipped', font=Font, fill='white')
    for Sheet, Writer in ((Canvas, Pen), (Errors, ErrorPen)):
        Sheet.paste(ReferenceTile.resize((160, 160), Image.Resampling.NEAREST), (16, 70))
        Writer.text((16, 236), 'Finite reference', font=Small, fill='white')
        Writer.text((16, 280), '16 pairs/pixel', font=Small, fill='white')
        Writer.text((16, 300), '32 candidates', font=Small, fill='white')
    CaseMetrics, CaseCosts = [], []
    for Method in range(4):
        X = 232 + Method * 220
        Estimates = [Estimate(Raw, Method, Slab) for Slab in (8, 9)]
        ArmMetrics, ArmCosts = [], []
        Work = [[], []]
        AddedVariance = 0.
        for P in range(Pairs):
            Pixel = Method * Samples + P
            W0, W1 = Float4(Raw, 11, Pixel)[:2]
            F0, F1 = Float4(Raw, 0, Pixel)[:3], Float4(Raw, 0, Pixel + Pairs)[:3]
            Visibility = Integer(Raw, 11, Pixel, 3)
            Shadowed = [[F[C] * bool(Visibility & (1 << I)) for C in range(3)]
                        for I, F in enumerate((F0, F1))]
            Mean = Float4(Raw, 9, Pixel)[:3]
            Sum = W0 + W1
            if Sum > 0:
                Expectation = [0., 0., 0.]
                for Weight, F in zip((W0, W1), Shadowed):
                    if Weight == 0:
                        assert all(C == 0 for C in F)
                        continue
                    Probability = Weight / Sum
                    Conditional = [C / (2 * Probability) for C in F]
                    for C in range(3):
                        Expectation[C] += Probability * Conditional[C]
                        AddedVariance += Probability * (Conditional[C] - Mean[C]) ** 2 / (3 * 1024 * 16 ** 2)
                for A, B in zip(Expectation, Mean):
                    Error = abs(A - B) / max(1., abs(B))
                    MaximumConditionalError = max(MaximumConditionalError, Error)
                    assert Error < 2e-7
            else:
                assert all(C == 0 for F in Shadowed for C in F)
                assert all(C == 0 for C in Float4(Raw, 8, Pixel)[:3])
                assert Integer(Raw, 10, Pixel) == 0xffffffff
            for Arm, Slab in enumerate((12, 13)):
                W = Float4(Raw, Slab, Pixel)
                assert W[0] == 2 and W[1] in (0, 1, 2) and W[2] in (0, 1, 2) and W[3] == sum(W[:3])
                if Arm == 0:
                    assert W[2] in (0, 1)
                Work[Arm].append(W[3])
            assert Work[0][-1] <= Work[1][-1]
        for Arm, E in enumerate(Estimates):
            Passed, Measurements = MeanGate(E, Reference)
            assert Passed, (Case, Method, Arm, Measurements)
            for C, (Mean, SE, Gate) in enumerate(Measurements):
                Means.append(f'|{Case}|{Methods[Method]}|{("RIS2", "raw2")[Arm]}|{C}|{Mean:.8g}|{SE:.8g}|{Gate}|')
            RMSE = math.sqrt(sum((E[P][C] - Reference[P][C]) ** 2 for P in range(1024) for C in range(3)) / 3072)
            Cross = sum((E[P][C] - Banks[0][P][C]) * (E[P][C] - Banks[1][P][C])
                        for P in range(1024) for C in range(3)) / 3072
            Calls = sum(Work[Arm]) / 1024
            ArmMetrics.append((RMSE, Cross))
            ArmCosts.append(Calls)
            Table.append(f'|{Case}|{Methods[Method]}|{("RIS2", "raw2")[Arm]}|{RMSE:.9g}|{Cross:.9g}|{Calls:.7f}|')
            Tile = Encode(E, f'{Prefix}-{Case}-{Method}-{Arm}.png', 64)
            ErrorTile = Encode([[abs(A[C] - B[C]) for C in range(3)] for A, B in zip(E, Reference)],
                               f'{Prefix}-error-{Case}-{Method}-{Arm}.png', 1024)
            Y = 80 + Arm * 360
            for Display, Sheet, Writer in ((Tile, Canvas, Pen), (ErrorTile, Errors, ErrorPen)):
                Sheet.paste(Display.resize((160, 160), Image.Resampling.NEAREST), (X, Y))
                Writer.text((X, Y - 24), Methods[Method], font=Small, fill='white')
                Writer.text((X, Y + 168), ('RIS2', 'raw2')[Arm], font=Font, fill='white')
                Writer.text((X, Y + 188), f'RMSE {RMSE:.5g}', font=Small, fill='white')
                Writer.text((X, Y + 208), f'Calls/pixel {Calls:.4f}', font=Small, fill='white')
        if not MeanGate(Estimate(Raw, Method, 14), Reference)[0]:
            NegativeFailures[Method] += 1
        QualityGates[Method].append(all(A <= B for A, B in zip(*ArmMetrics)))
        CostGates[Method].append(ArmCosts[0] < ArmCosts[1])
        TotalSaved += sum(Work[1]) - sum(Work[0])
        Differences = []
        for P in range(1024):
            Differences.append(sum((Estimates[0][P][C] - Banks[0][P][C]) * (Estimates[0][P][C] - Banks[1][P][C]) -
                                   (Estimates[1][P][C] - Banks[0][P][C]) * (Estimates[1][P][C] - Banks[1][P][C])
                                   for C in range(3)) / 3)
        Blocks = [sum(Differences[(Y * 4 + DY) * 32 + X * 4 + DX] for DY in range(4) for DX in range(4)) / 16
                  for Y in range(8) for X in range(8)]
        Mean = sum(Blocks) / 64
        SE = math.sqrt(sum((B - Mean) ** 2 for B in Blocks) / (63 * 64))
        Contrasts.append(f'|{Case}|{Methods[Method]}|{Mean:.8g}|{SE:.8g}|'
                         f'[{Mean - 1.96 * SE:.8g},{Mean + 1.96 * SE:.8g}]|{AddedVariance:.8g}|')
        CaseMetrics.append(ArmMetrics)
        CaseCosts.append(ArmCosts)
    Metrics.append(CaseMetrics)
    Costs.append(CaseCosts)
    for Sheet, Writer, Suffix in ((Canvas, Pen, 'Comparison'), (Errors, ErrorPen, 'Errors')):
        Writer.text((16, 840), 'Same candidates;actual attributed queries differ;no temporal reuse,denoiser or physical GPU timing.',
                    font=Small, fill='white')
        Name = f'{Prefix}{Suffix}-{Case}.png'
        Sheet.save(Images / Name)
        Sheets.append(Name)
assert NegativeFailures[2] > 0 and NegativeFailures[3] > 0, NegativeFailures
(Results / f'{Prefix}-comparison.md').write_text('\n'.join(Table + [''] + Contrasts) + '\n')
(Results / f'{Prefix}-means.md').write_text('\n'.join(Means) + '\n')
Summary = (f'PASS correctness:5 identical semantic rows;runtime hashes/build={HashCounts};three buffers;two modules.\n'
           'CPU replay/build:393216 candidate draws,196608 reservoir/raw pairs;72 RGB mean gates.\n'
           f'Wrong-M negative control failed layouts by proposal={NegativeFailures}.\n'
           f'Quality gates by proposal={QualityGates};all-three acceptance={[all(G) for G in QualityGates]}.\n'
           f'Actual-call gates by proposal={CostGates};saved suffix calls/build={TotalSaved}.\n'
           f'Conditional mean identity maximum error={MaximumConditionalError};added variance is nonnegative.\n'
           f'RMSE/cross-MSE by layout,proposal,RIS/raw={Metrics}\nActual calls/pixel={Costs}\n'
           'References:unchanged prior MIS integral,two1024-sample/pixel banks;prior manifest and seed separation verified.\n'
           'Host ASan/UBSan only;driver/SPIR-V uninstrumented;Windows/physical GPUs untested;production_promote=0.\n')
(Results / f'{Prefix}-parity.txt').write_text(Summary)
print(Summary)
Files = sorted([*Results.glob(f'{Prefix}*'), *Images.glob(f'{Prefix}*'), *Root.glob(f'Exhibits/LightCompute/Code/{Prefix}*'),
                Root / f'Exhibits/LightCompute/Run{Prefix}.sh', Root / f'Exhibits/LightCompute/{Prefix}.md'])
Manifest = Results / f'{Prefix}-evidence-hashes.txt'
Manifest.write_text(''.join(f'{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root)}\n'
                            for P in Files if P != Manifest))
