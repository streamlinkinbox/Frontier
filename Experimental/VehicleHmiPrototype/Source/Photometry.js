// LM-63 Type C, TILT=NONE. Values stay in candela until normalized for the shared LUT.
export function ParseIES(Text) {
    const Match=/TILT\s*=\s*([^\r\n]+)[\r\n]+/i.exec(Text);
    if(!Match || Match[1].trim().toUpperCase()!=='NONE') throw new Error('IES requires TILT=NONE (external/included tilt is not supported).');
    const N=Text.slice(Match.index+Match[0].length).trim().split(/[\s,]+/).map(Number);
    if(N.some(V=>!Number.isFinite(V))) throw new Error('IES contains invalid numeric data.');
    const NV=N[3], NH=N[4];
    if(N[5]!==1) throw new Error('Only LM-63 Type C photometry is supported.');
    if(!Number.isInteger(NV)||!Number.isInteger(NH)||NV<2||NH<1||NV>721||NH>721||NV*NH>262144) throw new Error('Invalid IES angle counts.');
    const V=N.slice(13,13+NV), H=N.slice(13+NV,13+NV+NH), Values=N.slice(13+NV+NH);
    if(Values.length<NV*NH || N[2]<=0) throw new Error('Incomplete IES candela table.');
    if(V.some((X,I)=>X<0||X>180||(I>0&&X<=V[I-1])) || H.some((X,I)=>X<0||X>360||(I>0&&X<=H[I-1]))) throw new Error('IES angles must be increasing and in range.');
    const Peak=Values.slice(0,NV*NH).reduce((A,B)=>Math.max(A,B),0)*N[2];
    if(!Number.isFinite(Peak)||!(Peak>0)||Values.some(X=>X<0)) throw new Error('IES candela must be nonnegative with a positive peak.');
    const Bracket=(A,X)=>{let I=0;while(I<A.length-2 && A[I+1]<X) I++;return [I,A.length===1?0:Math.max(0,Math.min(1,(X-A[I])/(A[I+1]-A[I])))];};
    const Sample=(Horizontal,Vertical)=>{
        if(Vertical<V[0]||Vertical>V[NV-1])return 0;
        let Angle=((Horizontal%360)+360)%360;
        if(H[NH-1]<=90){Angle=Math.min(Angle,360-Angle);if(Angle>90)Angle=180-Angle;}
        else if(H[NH-1]<=180)Angle=Math.min(Angle,360-Angle);
        const [J,T]=Bracket(V,Vertical),[I,U]=Bracket(H,Angle);
        const At=(Row)=>{const O=Math.min(Row,NH-1)*NV;return Values[O+J]*(1-T)+Values[O+J+1]*T;};
        return (At(I)*(1-U)+At(I+1)*U)*N[2];
    };
    const Table=new Float32Array(256);
    for(let H=0;H<16;H++)for(let V=0;V<16;V++)Table[H*16+V]=Sample(H*360/15,V*180/15)/Peak;
    return { Table, PeakCandela:Peak, Sample, Name:Text.match(/\[LUMINAIRE\]\s*([^\r\n]+)/i)?.[1]||'Imported Type C IES' };
}
