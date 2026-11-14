/*============================================================================================================================================
                                                      PARTICLEREDUCTIONCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang compute reduction accumulating alive slot count & atomic fixed-point bounding box into ParticleTelemetryCounters

import ParticleBuffers;

struct DispatchPushParams
{
    uint SlotOffset;
    uint SlotCount;
    uint ForceSourceCount;
    uint SpawnCursor;
};

[[vk::push_constant]]
DispatchPushParams PushParams;

[[vk::binding(3, 0)]] RWStructuredBuffer<ParticleSlot>              ParticleBuffer;
[[vk::binding(7, 0)]] RWStructuredBuffer<ParticleTelemetryCounters> TelemetryBuffer;

uint EncodeBoundMm(float Meters)
{
    float Mm = clamp(Meters * 1000.0 + 1000000.0, 0.0, 2000000000.0);
    return uint(Mm);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    if (DispatchThreadId.x >= PushParams.SlotCount)
    {
        return;
    }

    uint GlobalIdx = PushParams.SlotOffset + DispatchThreadId.x;
    ParticleSlot Slot = ParticleBuffer[GlobalIdx];
    if (Slot.PositionAge.w >= Slot.VelocityLifetime.w)
    {
        return;
    }

    InterlockedAdd(TelemetryBuffer[0].AliveCount, 1u);
    InterlockedMin(TelemetryBuffer[0].BoundsMinFixedX, EncodeBoundMm(Slot.PositionAge.x));
    InterlockedMin(TelemetryBuffer[0].BoundsMinFixedY, EncodeBoundMm(Slot.PositionAge.y));
    InterlockedMin(TelemetryBuffer[0].BoundsMinFixedZ, EncodeBoundMm(Slot.PositionAge.z));
    InterlockedMax(TelemetryBuffer[0].BoundsMaxFixedX, EncodeBoundMm(Slot.PositionAge.x));
    InterlockedMax(TelemetryBuffer[0].BoundsMaxFixedY, EncodeBoundMm(Slot.PositionAge.y));
    InterlockedMax(TelemetryBuffer[0].BoundsMaxFixedZ, EncodeBoundMm(Slot.PositionAge.z));
}
