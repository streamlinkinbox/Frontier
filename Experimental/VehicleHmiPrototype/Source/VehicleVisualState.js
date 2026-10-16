// Rigid wheel damage state plus cabin articulation. No visual tyre simulation.
export const VehicleRecordBytes = 144; // five cabin vec4s, four wheel vec4s
export class VehicleVisualState {
    EncodeRecord(Header, Wheels = []) {
        const Data = new Float32Array(VehicleRecordBytes / 4);
        const Cockpit = Header.Cockpit || { Anchor: [0, 0, 0], Scale: 1, Offset: [0, 0, 0] };
        Data.set([Header.SteeringAngle || 0, Header.SeeThroughGlass ? 1 : 0, 0, Header.Lamp || 0]);
        Data.set(Header.Column || [0, 0, 0], 4);
        Data.set(Header.Axis || [1, 0, 0], 8);
        Data.set([...Cockpit.Anchor, Cockpit.Scale], 12);
        Data.set([...(Header.Tablet?.Centre || [0,0,0]), Header.Tablet?.Scale || 1], 16);
        Wheels.forEach((Wheel, Index) => { Data[20 + Index * 4] = Wheel.Shredded ? 1 : 0; });
        return Data;
    }
}
