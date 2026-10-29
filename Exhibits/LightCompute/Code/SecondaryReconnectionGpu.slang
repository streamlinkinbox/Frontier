/*==========================================================================================================================================
                                                   SECONDARYRECONNECTIONGPU.SLANG
==========================================================================================================================================*/
// 🧩 Reconnect bounded diffuse secondary vertices with explicit area densities and receiver visibility.

#include "LightInfluence.slang"

float SecondaryCosine(float2 Point, float ReceiverX)
{
    float2 Delta = Point - float2(ReceiverX, 0);
    return rsqrt(1 + dot(Delta, Delta));
}
float SecondaryMeasure(float2 Point, float ReceiverX)
{
    float Cosine = SecondaryCosine(Point, ReceiverX);
    return Cosine * Cosine * Cosine;
}
bool SecondaryVisible(float2 Point, float ReceiverX, float Blocker)
{
    return Blocker == 0 || abs((Point.x + ReceiverX) * .5) > Blocker;
}
float SecondaryAngular(float2 Point, float ReceiverX, float Blocker)
{
    return SecondaryVisible(Point, ReceiverX, Blocker) && Point.x >= -.1
        ? .42 * SecondaryCosine(Point, ReceiverX) / 3.141592653589793 : 0;
}
float2 SecondaryProposal(inout uint Bits, float Mixture, out bool CosineProposal)
{
    float Choice = RandomUnit(Bits), U = RandomUnit(Bits), V = RandomUnit(Bits);
    CosineProposal = Choice < Mixture;
    if (!CosineProposal) return float2(U, V) - .5;
    float Radius = sqrt(U / (1 - U)), Phi = 6.283185307179586 * V;
    return Radius * float2(cos(Phi), sin(Phi));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GenerateSecondaryVertices(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint Receiver = Receivers[Pixel];
    uint Method = uint(Receiver.MotionPixel.z);
    float ReceiverX = Receiver.PositionHit.x, Blocker = Receiver.AlbedoIor.w;
    uint Bits = Extent.Sampling.x ^ ((Pixel % 4096u) * 747796405u + 2891336453u);
    float Mixture = Method == 4 || Method == 6 ? 1 : .75;
    float Floor = Method == 5 ? 0 : .01;
    float Sum = 0, Fresh = 0;
    float2 Selected = 0;
    uint Present = 0;
    for (uint Index = 0; Index < 8; ++Index)
    {
        bool CosineProposal;
        float2 Point = SecondaryProposal(Bits, Mixture, CosineProposal);
        if (Method == 6) Point.x += ReceiverX;
        bool Valid = all(abs(Point) <= .5);
        if (CosineProposal && !SecondaryVisible(Point, Method == 6 ? ReceiverX : 0, Blocker)) Valid = false;
        float Weight = 0;
        if (Valid)
        {
            ++Present;
            if (Method == 6) Fresh += Point.x >= -.1 ? .42 / 8 : 0;
            else
            {
                float DonorMeasure = SecondaryMeasure(Point, 0);
                float Density = (1 - Mixture) + (SecondaryVisible(Point, 0, Blocker)
                    ? Mixture * SecondaryCosine(Point, 0) * DonorMeasure / 3.141592653589793 : 0);
                Weight = (SecondaryAngular(Point, 0, Blocker) + Floor) * DonorMeasure / Density;
            }
        }
        Sum += Weight;
        float Uniform = RandomUnit(Bits);
        if (Weight > 0 && Uniform * Sum < Weight) Selected = Point;
    }
    float Estimate = Fresh;
    if (Method != 6 && Sum > 0)
    {
        float DonorMeasure = SecondaryMeasure(Selected, 0);
        float ReceiverMeasure = SecondaryMeasure(Selected, ReceiverX);
        float Contribution = SecondaryAngular(Selected, ReceiverX, Blocker);
        float DonorTarget = SecondaryAngular(Selected, 0, Blocker) + Floor;
        float ReceiverTarget = Contribution + Floor;
        bool AreaMeasure = Method == 1 || Method == 3 || Method == 4 || Method == 5;
        float Jacobian = ReceiverMeasure / DonorMeasure;
        float SourceWeight = Sum / (8 * DonorTarget * (AreaMeasure ? DonorMeasure : 1));
        float Target = ReceiverTarget * (AreaMeasure ? ReceiverMeasure : 1);
        float Factor = Method == 0 || Method == 3 ? Jacobian : 1;
        float CombinedWeight = Target * Factor * SourceWeight * 8;
        float Normalization = Target > 0 ? CombinedWeight / (8 * Target) : 0;
        Estimate = Contribution * (AreaMeasure ? ReceiverMeasure : 1) * Normalization;
    }
    OutputColour[Pixel] = float4(Estimate, float(Present), 8, Sum);
}

// The host's second compute dispatch is intentionally inert: all independent estimates are already stored.
[shader("compute")]
[numthreads(64, 1, 1)]
void ResolveSecondaryVertices(uint3 Dispatch : SV_DispatchThreadID)
{
    if (Dispatch.x >= Extent.Dimensions.x) return;
}
