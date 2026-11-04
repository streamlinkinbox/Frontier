/*==========================================================================================================================================
                                                                GLYPHRUN.CPP
==========================================================================================================================================*/
// 🧩 Glyph runs on CSS baselines (with the monospace fallback for code points DM Sans lacks)
//    and wrapped paragraph blocks.

#include "Engine/Interface/Controls/GlyphRun.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <cfloat>
#include <cstring>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLYPH RUN
//------------------------------------------------------------------------------------------------------------------------

ImFont* GlyphRun::ResolveFace(ImFont* Face) noexcept
{
    return Face != nullptr ? Face : ImGui::GetFont();
}

float GlyphRun::ResolveBaseline(float Top, float Size, float LineHeight) noexcept
{
    return Top + 0.5f * (LineHeight * Size - EmPixelHeight * Size) + EmAscent * Size;
}

float GlyphRun::Rasterize(ImDrawList* DrawList, const EditorTypefaces* Faces, ImFont* Face, float Size, ImVec2 Baseline, ImU32 Colour, const char* Text,
                         float Tracking, bool Measure)
{
    if (Text == nullptr || Text[0] == '\0') return 0.0f;
    ImFont*     Primary     = GlyphRun::ResolveFace(Face);
    ImFont*     Fallback    = GlyphRun::ResolveFace(Faces != nullptr ? Faces->Monospace : nullptr);
    const float PixelHeight = GlyphRun::EmPixelHeight * Size;
    const char* Glyph       = Text;
    const char* End         = Text + std::strlen(Text);
    float       Pen         = 0.0f;
    auto        FaceOf      = [&](unsigned int Codepoint) {
        return (Codepoint < 0x80u || Primary->IsGlyphInFont(static_cast<ImWchar>(Codepoint))) ? Primary : Fallback;
    };
    while (Glyph < End)
    {
        unsigned int Codepoint = 0;
        const int    Length    = ImTextCharFromUtf8(&Codepoint, Glyph, End);
        if (Length <= 0) break;
        ImFont*     RunFace = FaceOf(Codepoint);
        const char* RunEnd  = Glyph + Length;
        // 📝 Untracked runs of one face go out together, so the face's own spacing holds across the run.
        while (Tracking == 0.0f && RunEnd < End)
        {
            unsigned int Next      = 0;
            const int    NextBytes = ImTextCharFromUtf8(&Next, RunEnd, End);
            if (NextBytes <= 0 || FaceOf(Next) != RunFace) break;
            RunEnd += NextBytes;
        }
        if (!Measure && DrawList != nullptr)
        {
            ImFontBaked* Baked = RunFace == Primary ? nullptr : RunFace->GetFontBaked(PixelHeight);
            const float  Top   = Baseline.y - (Baked != nullptr ? Baked->Ascent : GlyphRun::EmAscent * Size);
            DrawList->AddText(RunFace, PixelHeight, ImVec2(Baseline.x + Pen, Top), Colour, Glyph, RunEnd);
        }
        Pen += RunFace->CalcTextSizeA(PixelHeight, FLT_MAX, 0.0f, Glyph, RunEnd).x + Tracking;
        Glyph = RunEnd;
    }
    return Pen;
}

float GlyphRun::Construct(const ControlColumn& Column, ImFont* Face, float Size, ImVec2 Baseline, ImU32 Colour, const char* Text,
                          float Tracking)
{
    return Rasterize(Column.QueryDrawList(), &Column.QueryFaces(), Face, Size, Baseline, Colour, Text, Tracking, false);
}

float GlyphRun::Measure(const ControlColumn& Column, ImFont* Face, float Size, const char* Text, float Tracking)
{
    return Rasterize(nullptr, &Column.QueryFaces(), Face, Size, ImVec2(0.0f, 0.0f), 0, Text, Tracking, true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARAGRAPH BLOCK
//------------------------------------------------------------------------------------------------------------------------

float ParagraphBlock::Construct(const ControlColumn& Column, ImFont* Face, float Size, float LineHeight, ImVec2 Min, float WrapWidth,
                                ImU32 Colour, const char* Text, bool Measure)
{
    if (Text == nullptr || Text[0] == '\0') return 0.0f;
    ImFont*     Primary     = GlyphRun::ResolveFace(Face);
    const float PixelHeight = GlyphRun::EmPixelHeight * Size;
    const float Leading     = LineHeight * Size;
    const char* LineStart   = Text;
    const char* End         = Text + std::strlen(Text);
    int         Lines       = 0;
    while (LineStart < End)
    {
        const char* LineEnd = Primary->CalcWordWrapPosition(PixelHeight, LineStart, End, WrapWidth);
        if (LineEnd <= LineStart) LineEnd = LineStart + 1;
        if (!Measure)
        {
            const std::string Line(LineStart, LineEnd);
            (void)GlyphRun::Construct(Column, Primary, Size, ImVec2(Min.x, GlyphRun::ResolveBaseline(Min.y + static_cast<float>(Lines) * Leading, Size, LineHeight)), Colour,
                                    Line.c_str());
        }
        ++Lines;
        LineStart = LineEnd;
        while (LineStart < End && (*LineStart == ' ' || *LineStart == '\n')) ++LineStart;
    }
    return static_cast<float>(Lines) * Leading;
}

} // namespace Frontier
