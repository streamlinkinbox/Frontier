/*============================================================================================================================================
                                                           FACETCDFPROFILE.SLANG
============================================================================================================================================*/
// 🧩 Decode runtime facet profiles,prepare retained area/CDF/PDF entries and sample cached continuous triangles.

// 📝 Includer provides LightInfluence.slang resources. No fixture score,transport or diagnostic output is defined here.
//    Header at Receivers[0].PositionHit is four uint32 bit patterns:source count,camera count,first quartet,capacity.
//    Profile quartets follow camera rows. Slot5 cache uses four header quartets,geometry-count entries and a guard.
//    Each entry is area/CDF/uniform-PDF float bits and packed identity;header words4/5 are total-area/area-PDF bits.
//    Slot7 counter20 counts area calls. Initialize lifetime words2/9/10;preserve cache and barrier before sampling.
#ifndef FRONTIER_FACET_CDF_PROFILE
#define FRONTIER_FACET_CDF_PROFILE

struct FacetCdfSample
{
    uint  Index;
    float U;
    float V;
    float Density;
};

uint FacetCdfHash(uint Bits)
{
    Bits ^= Bits >> 16; Bits *= 0x7feb352du;
    Bits ^= Bits >> 15; Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}

float FacetCdfArea(uint Index)
{
    return .5 * length(cross(Facets[Index].B.xyz - Facets[Index].A.xyz,
                            Facets[Index].C.xyz - Facets[Index].A.xyz));
}

uint FacetCdfSourceCount() { return asuint(Receivers[0].PositionHit.x); }
uint FacetCdfCameraCount() { return asuint(Receivers[0].PositionHit.y); }
uint FacetCdfSourceStart() { return asuint(Receivers[0].PositionHit.z); }

uint FacetCdfSourceIndex(uint Ordinal)
{
    return asuint(Receivers[FacetCdfSourceStart() + Ordinal / 4].PositionHit[Ordinal % 4]);
}

// 📝 Call only after successful preparation,status0 and matching cached support. Measure0 is uniform,1 is area.
//    The caller supplies any receiver-dependent score;this function returns only identity,U/V and area PDF.
FacetCdfSample ProposeFacetCdf(
    uint Seed,
    uint SampleOrdinal,
    uint Measure)
{
    float Choice = float(FacetCdfHash(Seed ^ (3 * SampleOrdinal + 1) * 0x9e3779b9u) >> 8) / 16777216.0;
    float First = float(FacetCdfHash(Seed ^ (3 * SampleOrdinal + 2) * 0x9e3779b9u) >> 8) / 16777216.0;
    float Second = float(FacetCdfHash(Seed ^ (3 * SampleOrdinal + 3) * 0x9e3779b9u) >> 8) / 16777216.0;
    uint Chosen = min(uint(Choice * float(FacetCdfSourceCount())), FacetCdfSourceCount() - 1);
    if (Measure == 1)
    {
        Chosen = FacetCdfSourceCount() - 1;
        for (uint Ordinal = 0; Ordinal < FacetCdfSourceCount(); ++Ordinal)
        {
            if (Choice < asfloat(Cells[16 + 4 * Ordinal + 1]))
            {
                Chosen = Ordinal;
                break;
            }
        }
    }
    FacetCdfSample Candidate = (FacetCdfSample)0;
    Candidate.Index = Cells[16 + 4 * Chosen + 3];
    float Root = sqrt(First);
    Candidate.U = 1 - Root;
    Candidate.V = Root * Second;
    Candidate.Density = Measure == 0 ? asfloat(Cells[16 + 4 * Chosen + 2]) : asfloat(Cells[5]);
    return Candidate;
}

float CountCdfArea(uint Index)
{
    InterlockedAdd(Counters[20], 1);
    Cells[3] += 1;
    Cells[10] += 1;
    return FacetCdfArea(Index);
}

uint ValidateFacetProfile()
{
    uint Support = FacetCdfSourceCount(), Cameras = FacetCdfCameraCount(), Start = FacetCdfSourceStart();
    uint Capacity = asuint(Receivers[0].PositionHit.w), Count = Extent.Dimensions.x;
    if (Support == 0 || Support > 16777216u || Support > Extent.Sampling.z || Cameras >= Count ||
        Start != Cameras + 1 || Start > Count || Capacity != (Support + 3) / 4 || Capacity != Count - Start)
        return 1;
    float Total = 0;
    for (uint Ordinal = 0; Ordinal < Support; ++Ordinal)
    {
        uint Index = FacetCdfSourceIndex(Ordinal);
        if (Index >= Extent.Sampling.z)
            return 2;
        for (uint Previous = 0; Previous < Ordinal; ++Previous)
            if (FacetCdfSourceIndex(Previous) == Index)
                return 2;
        float Area = CountCdfArea(Index);
        Cells[16 + 4 * Ordinal] = asuint(Area);
        Cells[16 + 4 * Ordinal + 3] = Index;
        if (!isfinite(Area) || Area <= 0)
            return 3;
        Total += Area;
    }
    if (!isfinite(Total) || Total <= 0 || !isfinite(1.0 / Total) || 1.0 / Total <= 0)
        return 3;
    Cells[4] = asuint(Total);
    Cells[5] = asuint(1.0 / Total);
    float Boundary = 0;
    for (uint Ordinal = 0; Ordinal < Support; ++Ordinal)
    {
        float Area = asfloat(Cells[16 + 4 * Ordinal]);
        float Next = Boundary + Area / Total;
        float Density = 1.0 / float(Support) / Area;
        if (!isfinite(Next) || Next <= Boundary || (Ordinal + 1 < Support && Next >= 1) ||
            !isfinite(Density) || Density <= 0)
            return 3;
        Cells[16 + 4 * Ordinal + 1] = asuint(Next);
        Cells[16 + 4 * Ordinal + 2] = asuint(Density);
        Boundary = Next;
    }
    return 0;
}

#endif
