/*==========================================================================================================================================
                                                            CONSTRUCTPANEL.H
==========================================================================================================================================*/
// 🧩 The Construct dialog of the Project Zero editor (ConstructPanel.jsx, Editor.css .construct-*): a category rail, a
//    searchable matrix of what can be added (the engine shapes and the level's constructible record types), then the
//    chosen one's name and properties, and "Add to scene". Ctrl+A or the Outliner's '+' opens it; the host adds the result.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One tile of the matrix: an engine shape (Shape: Identifier is its ShapeName) or a record type (Identifier is the type).
struct ConstructCatalogueEntry
{
    std::string           Identifier{};
    std::string           Caption{};
    std::string           Glyph{};                     // [-] GlyphSymbol name
    std::string           Group{};                     // [-] Environment, Weather, Forces, Cameras, Geometry or Lighting
    std::string           Summary{};                   // [-] the properties step's note
    bool                  Shape = false;
    std::array<double, 3> Size{1.0, 1.0, 1.0};         // [m] a shape's bounding box
    std::array<float, 3>  Albedo{0.55f, 0.55f, 0.55f}; // [-] a shape's surface (linear)
};

// 📝 What "Add to scene" hands the host.
struct ConstructOrder
{
    ConstructCatalogueEntry Entry{};
    std::string             Name{};
    float                   Size    = 1.0f;     // [-] a shape's uniform scale
    std::array<float, 3>    Albedo{};           // [-] a shape's surface (linear)
    bool                    Surfaced = false;   // [-] the surface differs from the shape's own: the object owns its surface
    bool                    Visible  = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCT PANEL
//------------------------------------------------------------------------------------------------------------------------

class ConstructPanel
{
  public:
    void ConfigureCatalogue(std::vector<ConstructCatalogueEntry> Entries) { Catalogue_ = std::move(Entries); }
    [[nodiscard]] const std::vector<ConstructCatalogueEntry>& QueryCatalogue() const noexcept { return Catalogue_; }

    void Open();
    // 📝 Opens on one category of the rail ("Forces": the Force Field page's "Add a localized force").
    void Open(std::string_view Group);
    void Close();
    [[nodiscard]] bool QueryOpen() const noexcept { return Open_; }

    // 📝 Draws the dialog (a modal over the editor) while it is open; the order once "Add to scene" was pressed.
    [[nodiscard]] std::optional<ConstructOrder> Construct(float Scale);

  private:
    std::vector<ConstructCatalogueEntry> Catalogue_{};
    bool                                 Open_    = false;
    bool                                 Raise_   = false;   // [-] the modal opens on the next Construct
    bool                                 Dismiss_ = false;   // [-] closed from outside: the modal closes on the next Construct
    std::string                          Group_   = "All";
    std::array<char, 96>                 Query_{};
    int                                  Picked_  = -1;      // [-] the catalogue entry of the properties step
    std::array<char, 64>                 Name_{};
    float                                Size_    = 1.0f;
    std::array<float, 3>                 Albedo_{};
    bool                                 Visible_ = true;
    bool                                 FocusSearch_ = false;
};

} // namespace Frontier
