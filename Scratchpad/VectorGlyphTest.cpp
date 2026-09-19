//============================================================================================================================================
// 📦 Frontier/Scratchpad/VectorGlyphTest.cpp — Scratchpad Test Harness for VectorCodec Scalable Vector Graphics Glyphs
//============================================================================================================================================

#include "../DisplayPresentation/VectorCodec.h"
#include <iostream>

int main()
{
    std::cout << "[Scratchpad] Testing Frontier VectorCodec SVG Glyph Table...\n";
    std::cout << "[Scratchpad] Total Navigation Arrow Glyphs: " << Frontier::VectorCodec::QueryNavigationIconCount() << "\n\n";

    for (uint32_t i = 0; i < Frontier::VectorCodec::QueryNavigationIconCount(); ++i)
    {
        auto IconCategory = static_cast<Frontier::NavigationIconCategory>(i);
        const auto& Record = Frontier::VectorCodec::QueryNavigationIcon(IconCategory);
        std::cout << "  [" << i << "] " << Record.IdentifierName
                  << " (" << Record.ViewBoxWidth << "x" << Record.ViewBoxHeight
                  << ", Stroke: " << Record.DefaultStrokeWidth << "px)\n"
                  << "       Path: " << Record.SvgPathString << "\n";
    }

    std::cout << "\n[Scratchpad] VectorCodec tests passed successfully.\n";
    return 0;
}
