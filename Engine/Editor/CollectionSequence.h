//============================================================================================================================================
//                                                           COLLECTIONSEQUENCE.H
//============================================================================================================================================
// 📦 Iterative roster census and bounded collection browsing; all depth-dependent storage lives on the heap.

#pragma once

#include "EditorInstance.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace Frontier {

struct CollectionSequence
{
    struct RowSelection
    {
        uint32_t Index;
        bool     Visible;     // the effective visibility, enclosing folders included
        bool     Own = true;  // its own flag; Own && !Visible is FolderInventory's Inherited
    };

    uint64_t                  SelectedKey = 0;
    uint32_t                  Total = 0, Direct = 0, Visible = 0, Locked = 0, Folders = 0, MaximumDepth = 0;
    uint32_t                  Constructed = 0;   // FolderInventory.Constructed: rows with a preview marker
    uint32_t                  Categories[static_cast<unsigned>(EditorInstanceCategory::Count)]{};
    uint32_t                  Glyphs[static_cast<unsigned>(EditorGlyph::Count)]{};
    uint32_t                  AutoCategories[static_cast<unsigned>(EditorInstanceCategory::Count)]{};
    int                       Page = 0, PageSize = 25, Visibility = 0, Category = 0, GlyphFilter = 0, Sort = 0;
    bool                      DirectOnly = false;
    char                      Search[96]{};
    std::vector<RowSelection> Matches;
    std::vector<RowSelection> EnclosingRows;

    static bool Contains(const char* Text, const char* Query) noexcept
    {
        for (; *Text; ++Text)
        {
            const char* Character = Text;
            const char* Requested = Query;
            while (*Character && *Requested && std::tolower(static_cast<unsigned char>(*Character)) ==
                   std::tolower(static_cast<unsigned char>(*Requested)))
            {
                ++Character;
                ++Requested;
            }
            if (!*Requested) return true;
        }
        return !*Query;
    }

    void Traverse(const EditorInstance* Rows, uint32_t Count, uint32_t Selected)
    {
        Total = Direct = Visible = Locked = Folders = MaximumDepth = Constructed = 0;
        std::fill(std::begin(Categories), std::end(Categories), 0u);
        std::fill(std::begin(Glyphs), std::end(Glyphs), 0u);
        std::fill(std::begin(AutoCategories), std::end(AutoCategories), 0u);
        Matches.clear();
        EnclosingRows.clear();
        if (!Rows || Selected >= Count) return;
        if (SelectedKey != Rows[Selected].InspectorKey)
        {
            SelectedKey = Rows[Selected].InspectorKey;
            Page = Category = GlyphFilter = Visibility = Sort = 0;
            DirectOnly = false;
            Search[0] = 0;
        }
        // 📝 Scan enclosing rows too: a locally visible row can inherit a hidden enclosing folder.
        for (uint32_t Index = 0; Index < Count; ++Index)
        {
            const auto& Row = Rows[Index];
            if (Index > Selected && Row.Depth <= Rows[Selected].Depth) break;
            while (!EnclosingRows.empty() && Rows[EnclosingRows.back().Index].Depth >= Row.Depth)
                EnclosingRows.pop_back();
            const bool Effective = Row.Visible && (EnclosingRows.empty() || EnclosingRows.back().Visible);
            if (Index > Selected)
            {
                const uint32_t Distance = Row.Depth - Rows[Selected].Depth;
                ++Total;
                Direct += Distance == 1;
                Visible += Effective;
                Locked += Row.Locked || Row.Pinned;
                Folders += Row.Category == EditorInstanceCategory::Folder;
                MaximumDepth = std::max(MaximumDepth, Distance);
                // Editor.jsx seats a preview marker on every row it constructed from a base mesh, and
                //    FolderInventory counts exactly those.
                Constructed += BaseMeshPrimitive(Row.Artwork) != nullptr;
                const unsigned Group = static_cast<unsigned>(Row.Category);
                if (Group < static_cast<unsigned>(EditorInstanceCategory::Count)) ++Categories[Group];
                const unsigned Glyph = static_cast<unsigned>(Row.Glyph);
                if (Glyph < static_cast<unsigned>(EditorGlyph::Count)) ++Glyphs[Glyph];
                if (Row.Glyph == EditorGlyph::Auto && Group < static_cast<unsigned>(EditorInstanceCategory::Count)) ++AutoCategories[Group];
                if ((!DirectOnly || Distance == 1) && (!Category || Group + 1 == unsigned(Category)) &&
                    (!Visibility || Effective == (Visibility == 1)) && (!GlyphFilter || int(Glyph) == GlyphFilter) && Contains(Row.Label, Search))
                    Matches.push_back({Index, Effective, Row.Visible});
            }
            EnclosingRows.push_back({Index, Effective, Row.Visible});
        }
        if(Sort==0)std::sort(Matches.begin(),Matches.end(),[&](const RowSelection& A,const RowSelection& B){return std::strcmp(Rows[A.Index].Label,Rows[B.Index].Label)<0;});
        else if(Sort==1)std::sort(Matches.begin(),Matches.end(),[&](const RowSelection& A,const RowSelection& B){const auto CA=static_cast<unsigned>(Rows[A.Index].Category),CB=static_cast<unsigned>(Rows[B.Index].Category);return CA!=CB?CA<CB:std::strcmp(Rows[A.Index].Label,Rows[B.Index].Label)<0;});
        else std::sort(Matches.begin(),Matches.end(),[&](const RowSelection& A,const RowSelection& B){return Rows[A.Index].Depth!=Rows[B.Index].Depth?Rows[A.Index].Depth<Rows[B.Index].Depth:std::strcmp(Rows[A.Index].Label,Rows[B.Index].Label)<0;});
        PageSize = std::clamp(PageSize, 1, 100);
        Page = std::clamp(Page, 0, std::max(0, (int(Matches.size()) - 1) / PageSize));
    }
};

} // namespace Frontier
