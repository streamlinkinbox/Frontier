//============================================================================================================================
// 🧩 TyreOracle.mjs — reference values from the HTML demonstration's own code, for the VehiclePhysics exhibit.
// 📝 Imports the demo's TyreSlipSolver.js and TyreThermalSolver.js unmodified and dumps MF6.1 force tables, relaxation
//    lengths and a thermal trajectory; the FEM vertex deformation is transliterated from VehicleVisual.wgsl below
//    (WGSL cannot run under node) with float32 rounding at the same points the shader rounds.
//    Output: whitespace tables on stdout, one section per header line starting with '#'.
//============================================================================================================================

import { TyreSlipSolver } from "../../../Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source/TyreSlipSolver.js";
import { TyreThermalSolver, IgnitionModes } from "../../../Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source/TyreThermalSolver.js";

const F = (x) => Number(x).toPrecision(17);

// ---------------------------------------------------------------- MF6.1 pure + combined slip
const Tyres = new TyreSlipSolver();
const Loads = [500, 2000, 4000, 8000, 12000];
const Kappas = [-0.92, -0.5, -0.2, -0.1, -0.05, -0.02, 0, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 1.5];
const Alphas = [-1.3, -0.6, -0.3, -0.15, -0.05, 0, 0.05, 0.15, 0.3, 0.6, 1.3];
console.log("# MF61 loads kappas alphas: Fx(kappa,camber,load) rows, Fy(alpha,camber,load) rows, Combine rows");
for (const Load of Loads)
    for (const Camber of [0, 0.03, -0.05])
    {
        for (const Kappa of Kappas)
            console.log(`FX ${F(Kappa)} ${F(Camber)} ${F(Load)} ${F(Tyres.LongitudinalForce(Kappa, Camber, Load))}`);
        for (const Alpha of Alphas)
            console.log(`FY ${F(Alpha)} ${F(Camber)} ${F(Load)} ${F(Tyres.LateralForce(Alpha, Camber, Load))}`);
    }
for (const Load of Loads)
    for (const [Kappa, Alpha] of [[0.1, 0.1], [-0.3, 0.25], [0.8, -0.4], [0, 0], [1.2, 0.9], [-0.85, -1.1]])
    {
        const [Fx, Fy] = Tyres.Combine(Kappa, Alpha, 0.02, Load);
        console.log(`CB ${F(Kappa)} ${F(Alpha)} ${F(0.02)} ${F(Load)} ${F(Fx)} ${F(Fy)}`);
    }
for (const Load of [0, 250, 1000, 4000, 9000, 24000])
{
    const [Sx, Sy] = Tyres.ResolveRelaxationLengths(Load);
    console.log(`RL ${F(Load)} ${F(Sx)} ${F(Sy)}`);
}

// ---------------------------------------------------------------- Thermal trajectory (one sliding wheel)
console.log("# THERMAL: seconds surface carcass gas pressure grip treadmass (25 kW slide 3 s, then 5 s rolling)");
{
    const Thermal = new TyreThermalSolver(4);
    const Mode = IgnitionModes[0];
    const Readings = [0, 1, 2, 3].map(() => ({ SlidingPower: 0, ContactFraction: 1, Airspeed: 8, Shredded: false }));
    for (let Step = 0; Step <= 96; ++Step)
    {
        const T = Step / 12;
        const W = Thermal.Wheels[2];
        console.log(`TH ${F(T)} ${F(W.Surface)} ${F(W.Carcass)} ${F(W.Gas)} ${F(W.GaugePressure)} ${F(W.Grip)} ${F(W.TreadMass)}`);
        Readings[2].SlidingPower = T < 3 ? 25000 : 0;
        Thermal.Advance(Readings, 1 / 12, Mode);
    }
}

// ---------------------------------------------------------------- FEM vertex deformation (WGSL transliteration)
console.log("# FEM: record deflection pressure lateral, vertex rows in(xyz) out(xyz)");
function ResolveWheelVertex(Record, Position)
{
    const TyreRimRadius = 0.3448, TyreOuterRadius = 0.50855, PI = Math.PI;
    const f32 = Math.fround;
    let Out = Position.slice();
    const Deflection = Record[1];
    if (Deflection <= 1.0e-5) return Out;
    const ContactAngle = Record[0];
    const Pressure = Math.max(0, Record[2]);
    const LateralDistortion = Record[3];
    const R = f32(Math.hypot(Position[0], Position[2]));
    if (R <= TyreRimRadius + 1.0e-4) return Out;
    const Phi = Math.atan2(Position[2], Position[0]);
    let DPhi = Phi - ContactAngle;
    DPhi = DPhi - 2 * PI * Math.round(DPhi / (2 * PI));
    const PNorm = Math.max(0.05, Pressure / 220000);
    const PatchSpan = Math.min(1.15, Math.max(0.20, 0.44 / Math.sqrt(Math.max(0.15, PNorm))));
    const RadialFrac = Math.min(1, Math.max(0, (R - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius)));
    let DR = 0, DY = 0;
    const AbsDPhi = Math.abs(DPhi);
    if (AbsDPhi < PatchSpan)
    {
        const U = AbsDPhi / PatchSpan;
        const Envelope = 0.5 * (1 + Math.cos(U * PI));
        DR = -Deflection * Envelope * RadialFrac;
        const SidewallFactor = Math.sin(RadialFrac * PI);
        const BulgeIntensity = 0.48 / (0.4 + 0.6 * Math.sqrt(PNorm));
        DY = Math.sign(Position[1]) * (Deflection * BulgeIntensity * Envelope * SidewallFactor);
    }
    const WaveOuter = PatchSpan * 1.5;
    if (AbsDPhi >= PatchSpan && AbsDPhi < WaveOuter)
    {
        const V = (AbsDPhi - PatchSpan) / (WaveOuter - PatchSpan);
        DR += Math.sin(V * PI) * (0.12 * Deflection * RadialFrac);
    }
    if (AbsDPhi < PatchSpan * 1.2)
    {
        const UShear = AbsDPhi / (PatchSpan * 1.2);
        DY += LateralDistortion * 0.5 * (1 + Math.cos(UShear * PI)) * RadialFrac;
    }
    const NewR = Math.max(TyreRimRadius, R + DR);
    return [NewR * Math.cos(Phi), Position[1] + DY, NewR * Math.sin(Phi)];
}
const Records = [
    [-Math.PI / 2, 0.014, 220000, 0],
    [-Math.PI / 2, 0.060, 30000, 0],
    [-Math.PI / 2, 0.008, 350000, 0.02],
    [0.7, 0.030, 150000, -0.03]
];
for (const Record of Records)
{
    console.log(`FR ${Record.map(F).join(" ")}`);
    for (let Sector = 0; Sector < 24; ++Sector)
        for (const Radius of [0.36, 0.43, 0.50855])
            for (const Y of [-0.19, -0.08, 0, 0.08, 0.19])
            {
                const Angle = Sector / 24 * 2 * Math.PI;
                const Vertex = [Radius * Math.cos(Angle), Y, Radius * Math.sin(Angle)];
                const Out = ResolveWheelVertex(Record, Vertex);
                console.log(`FV ${Vertex.map(F).join(" ")} ${Out.map(F).join(" ")}`);
            }
}
