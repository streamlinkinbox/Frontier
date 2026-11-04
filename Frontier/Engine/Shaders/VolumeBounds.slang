//==========================================================================================================================================
//                                                        VOLUME BOUNDS SLANG
//==========================================================================================================================================
// 🧩 Y-up finite-volume bounds and smooth density envelopes, including rays starting inside and axis-parallel rays.

float3 ResolveVolumeHalfExtent(float3 HalfExtent, uint Shape)
{
    float3 Half = max(HalfExtent, float3(0.001f));
    if (Shape == 2u) Half = float3(Half.x);
    if (Shape == 3u) Half.z = Half.x;
    return Half;
}

float EvaluateVolumeEnvelope(float3 Relative, float3 HalfExtent, uint Shape, float EdgeMetres)
{
    float3 Half = ResolveVolumeHalfExtent(HalfExtent, Shape);
    float Soft = max(0.0001f, EdgeMetres);
    if (Shape == 1u)
    {
        float3 Fraction = saturate((Half - abs(Relative)) / Soft);
        float3 Envelope = Fraction * Fraction * (3.0f - 2.0f * Fraction);
        return Envelope.x * Envelope.y * Envelope.z;
    }
    if (Shape == 3u && Relative.y < 0.0f) return 0.0f;
    float Margin = (1.0f - length(Relative / Half)) * min(Half.x, min(Half.y, Half.z));
    if (Shape == 3u) Margin = min(Margin, Relative.y);
    return smoothstep(0.0f, Soft, Margin);
}

bool IntersectVolumeBounds(float3 Origin,
                           float3 Direction,
                           float3 Centre,
                           float3 HalfExtent,
                           uint Shape,
                           float Limit,
                           out float NearDistance,
                           out float FarDistance)
{
    float3 Half = ResolveVolumeHalfExtent(HalfExtent, Shape);
    float3 Relative = Origin - Centre;
    NearDistance = 0.0f;
    FarDistance = Limit;
    if (Shape != 1u)
    {
        float3 ScaledOrigin = Relative / Half;
        float3 ScaledDirection = Direction / Half;
        float A = dot(ScaledDirection, ScaledDirection);
        float B = dot(ScaledOrigin, ScaledDirection);
        float C = dot(ScaledOrigin, ScaledOrigin) - 1.0f;
        float Discriminant = B * B - A * C;
        if (Discriminant < 0.0f || A <= 1.0e-20f) return false;
        float Root = sqrt(max(0.0f, Discriminant));
        NearDistance = max(0.0f, (-B - Root) / A);
        FarDistance = min(Limit, (-B + Root) / A);
        if (Shape == 3u)
        {
            if (abs(Direction.y) < 1.0e-7f)
            {
                if (Relative.y < 0.0f) return false;
            }
            else
            {
                float Plane = -Relative.y / Direction.y;
                if (Direction.y > 0.0f) NearDistance = max(NearDistance, Plane);
                else FarDistance = min(FarDistance, Plane);
            }
        }
        return FarDistance > NearDistance;
    }
    for (uint Axis = 0u; Axis < 3u; ++Axis)
    {
        if (abs(Direction[Axis]) < 1.0e-7f)
        {
            if (abs(Relative[Axis]) >= Half[Axis]) return false;
        }
        else
        {
            float First = (-Half[Axis] - Relative[Axis]) / Direction[Axis];
            float Last = (Half[Axis] - Relative[Axis]) / Direction[Axis];
            NearDistance = max(NearDistance, min(First, Last));
            FarDistance = min(FarDistance, max(First, Last));
        }
    }
    return FarDistance > NearDistance;
}
