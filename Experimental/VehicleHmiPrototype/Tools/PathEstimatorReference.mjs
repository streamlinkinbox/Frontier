// Small, independent CPU oracle for generalized RIS. This is NOT a renderer or
// a browser fallback. Tests compare its expectation with exact finite integrals.
export function RandomStream(Seed = 1) {
    let State = Seed >>> 0;
    return () => {
        State = (Math.imul(State, 747796405) + 2891336453) >>> 0;
        const Word = Math.imul((State >>> ((State >>> 28) + 4)) ^ State, 277803737) >>> 0;
        return (((Word >>> 22) ^ Word) >>> 8) / 16777216;
    };
}
export function Draw(Pdf, Random) {
    const U = Random(); let Sum = 0;
    for (let I = 0; I < Pdf.length; ++I) { Sum += Pdf[I]; if (U < Sum) return I; }
    return Pdf.length - 1;
}
export function Offer(R, Y, Target, Weight, Count, Random) {
    R.M += Count; R.Sum += Weight;
    if (Weight > 0 && Random() * R.Sum < Weight) { R.Y = Y; R.Target = Target; }
}
export function Finalize(R) {
    R.W = R.Target > 0 && R.M > 0 ? R.Sum / (R.M * R.Target) : 0;
    return R;
}
export function Fresh(Pdf, Target, Count, Random) {
    const R = { Y: 0, M: 0, Sum: 0, Target: 0, W: 0 };
    for (let I = 0; I < Count; ++I) { const Y = Draw(Pdf, Random); Offer(R, Y, Target[Y], Target[Y] / Pdf[Y], 1, Random); }
    return Finalize(R);
}
export function Combine(Reservoirs, Target, Random, Limit = Infinity) {
    const R = { Y: 0, M: 0, Sum: 0, Target: 0, W: 0 };
    for (const Other of Reservoirs) {
        const M = Math.min(Other.M, Limit);
        Offer(R, Other.Y, Target[Other.Y], Target[Other.Y] * Other.W * M, M, Random);
    }
    return Finalize(R);
}
export function ReconnectionFactor(From, Position, Normal, Area) {
    const Delta = Position.map((V, I) => V - From[I]);
    const D2 = Delta.reduce((Sum, V) => Sum + V * V, 0);
    return Area * Math.abs(Normal.reduce((Sum, N, I) => Sum - N * Delta[I], 0)) / Math.pow(D2, 1.5);
}
