/*==========================================================================================================================================
                                                       FOGFACETSCATTERING.H
==========================================================================================================================================*/
// 🧩 Native area-to-solid-angle fog illumination with independent triangle visibility and analytic
// extinction.

//------------------------------------------------------------------------------------------------------------------------
//                                                 EMISSIVE VOLUME QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

SpectrumRgb IntegrateFogFacets(
    const LightComputeArchive&      Scene,
    const TriangleVisibilitySpace& Space,
    const FogParameters&           Medium,
    SpatialVector                  At,
    SpatialVector                  ViewDirection,
    double                         G,
    double                         LightDistance,
    bool                           Occlusion,
    uint32_t&                      Bits,
    uint32_t                       Samples)
{
    using namespace TransportReference;
    SpectrumRgb Sum;
    uint32_t Offset = uint32_t(Scene.Camera[10][2]), Count = uint32_t(Scene.Angles[Offset]);
    if (Count == 0)
        return Sum;
    for (uint32_t I = 0; I < Samples; ++I)
    {
        double Draw = EmissiveQuadrature::Random(Bits);
        uint32_t Selected = Count - 1;
        for (uint32_t J = 0; J < Count; ++J)
            if (Draw < Scene.Angles[Offset + 1 + J * 4 + 3])
            {
                Selected = J;
                break;
            }
        uint32_t Entry = Offset + 1 + Selected * 4;
        const auto &F = Scene.Surfaces[uint32_t(Scene.Angles[Entry])];
        double Root = std::sqrt(EmissiveQuadrature::Random(Bits)), V = EmissiveQuadrature::Random(Bits);
        auto Position = Add(Mul(Vector(F.A), 1 - Root),
                            Add(Mul(Vector(F.B), Root * (1 - V)), Mul(Vector(F.C), Root * V)));
        auto Delta = Sub(Position, At);
        double Squared = Dot(Delta, Delta), Distance = std::sqrt(Squared);
        if (Distance <= .004)
            continue;
        auto Toward = Mul(Delta, 1 / Distance);
        auto Normal = Unit(Cross(Sub(Vector(F.B), Vector(F.A)), Sub(Vector(F.C), Vector(F.A))));
        double Geometry = std::abs(Dot(Normal, Toward));
        if (Geometry == 0 ||
            (Occlusion && Space.Occluded(Add(At, Mul(Toward, .002)), Toward, 0, Distance - .004)))
            continue;
        double Weight = HenyeyGreensteinPhase(Dot(ViewDirection, Toward), G) * Geometry *
                        FogTransmittance(Medium, At, Toward, std::min(Distance, LightDistance)) *
                        Scene.Angles[Entry + 2] / (Squared * Scene.Angles[Entry + 1] * Samples);
        Sum.R += F.Emissive[0] * Weight;
        Sum.G += F.Emissive[1] * Weight;
        Sum.B += F.Emissive[2] * Weight;
    }
    return Sum;
}
