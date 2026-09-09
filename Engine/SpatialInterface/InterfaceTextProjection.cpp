//============================================================================================================================================
//                                                  INTERFACETEXTPROJECTION.CPP
//============================================================================================================================================

#include "InterfaceTextProjection.h"

namespace Frontier {

namespace {

// A space advances the pen but produces nothing. Anything the glyph table does not know still advances, so a
//    label containing an unsupported character keeps its spacing rather than collapsing — the gap is visible and
//    obviously wrong, which is the honest failure for a missing glyph.
[[nodiscard]] bool ProducesFigure(char Character) noexcept
{
    return Character != ' ' && Character != '\t';
}

} // namespace

float InterfaceTextProjection::MeasureWidth(std::string_view Text, const TextPlacement& Placement) noexcept
{
    if (Text.empty()) return 0.0f;
    const float Step = Placement.CapHeight * Placement.Advance;
    // N characters occupy N steps of pen travel; the visible ink of the last one is narrower than a full step,
    //    but using the pen width keeps Compose's alignment and this measurement in exact agreement.
    return Step * static_cast<float>(Text.size());
}

uint32_t InterfaceTextProjection::Compose(InterfaceStructure& Structure, uint32_t Ancestor,
                                          std::string_view Text, const TextPlacement& Placement) noexcept
{
    if (Text.empty()) return 0u;

    const float Step  = Placement.CapHeight * Placement.Advance;
    const float Width = MeasureWidth(Text, Placement);

    // Alignment shifts the pen start. Computed once rather than per glyph so a re-laid label cannot drift.
    float PenX = Placement.OriginX;
    if      (Placement.Alignment == TextAlignment::Centre) PenX -= Width * 0.5f;
    else if (Placement.Alignment == TextAlignment::Right)  PenX -= Width;

    uint32_t Created = 0u;
    for (char Character : Text)
    {
        if (ProducesFigure(Character))
        {
            InterfaceFigure Figure;
            Figure.Category = InterfaceCategory::Glyph;

            // The em box is square in the glyph table, so the figure's half extent is half the cap height in both
            //    axes; the shader maps the box back to 0..1 before evaluating.
            Figure.HalfWidth  = Placement.CapHeight * 0.5f;
            Figure.HalfHeight = Placement.CapHeight * 0.5f;

            // The glyph draws about the figure centre, and the pen sits at the left of the cell, so advance by
            //    half a step to centre this character in its own slot.
            Figure.Placement.Origin = PlaneOrigin{ PenX + Step * 0.5f,
                                                   Placement.OriginY + Placement.CapHeight * 0.5f,
                                                   Placement.OriginZ };

            Figure.ScalarAlpha  = static_cast<float>(static_cast<unsigned char>(Character));
            Figure.ScalarBeta   = Placement.StrokeWidth;
            Figure.Palette      = Placement.Palette;
            Figure.TintOverride = Placement.TintOverride;
            Figure.OrderingRank = Placement.OrderingRank;

            const uint32_t Ordinal = Structure.Construct(Figure);
            if (Ancestor != InterfaceStructure::Detached) (void)Structure.Attach(Ordinal, Ancestor);
            ++Created;
        }
        PenX += Step;
    }
    return Created;
}

} // namespace Frontier
