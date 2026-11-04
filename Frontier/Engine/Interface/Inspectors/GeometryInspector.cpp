/*==========================================================================================================================================
                                                           GEOMETRYINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 The geometry inspector: the Project Zero editor's object inspector for the level's scene objects (the Geometry record),
//    with the transform, the quick controls, a Blender-style modifier stack, the material channels and the fracture.

#include "Engine/Interface/Inspectors/GeometryInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/NotesField.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Controls/RecordHeading.h"
#include "Engine/Interface/FractureEditor.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/Geometry/GeometrySections.h"
#include "Engine/Scene/SceneDirectory.h"

#endif

#include <cstdio>

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER ROW
//------------------------------------------------------------------------------------------------------------------------

std::string GeometryInspector::QueryDirectoryReadout() const
{
    const auto& Resolved = Parameters.Resolved;
    if (!Resolved.Bound) return {};
    if (Resolved.Authored <= 1 && Parameters.Modifiers.empty()) return {};
    char Text[48];
    std::snprintf(Text, sizeof(Text), "%u\xC3\x97", Resolved.Shown);
    return Text;
}

const char* GeometryInspector::QueryDirectoryGlyph() const noexcept
{
    return Parameters.GlyphName.empty() ? nullptr : Parameters.GlyphName.c_str();
}

RecordOperationalCondition GeometryInspector::QueryDirectoryCondition() const noexcept
{
    return Parameters.Visible && Parameters.Resolved.Bound ? RecordOperationalCondition::Live : RecordOperationalCondition::Standby;
}

#endif

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRACTURE EDITOR
//------------------------------------------------------------------------------------------------------------------------

void GeometryInspector::ConstructEditor(RecordToken Token)
{
    FractureEditor::ConstructInterface(Token, FractureView);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE
//------------------------------------------------------------------------------------------------------------------------

void GeometryInspector::ConstructInspector()
{
    const auto        Theme    = ControlTheme::ResolveGeometry();
    const auto        Faces    = EditorTypefaces::Resolve();
    ControlColumn     Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    const RecordToken Token    = InspectorRuntime::Instance().QueryInspectedToken();
    const std::string Name     = SceneDirectory::Instance().QueryName(Token);
    const auto&       Resolved = Parameters.Resolved;

    // ① the heading: Inspector / Geometry / group, the object's name, what it is made of; the notes under it when open
    std::string Breadcrumb = "Inspector / Geometry";
    if (!Parameters.GroupName.empty()) Breadcrumb += " / " + Parameters.GroupName;
    std::string Subtitle = !Parameters.ShapeName.empty() ? "Engine shape" : (Parameters.ClaimsArchive ? "Geometry" : "Duplicate");
    if (Resolved.Bound)
    {
        for (size_t Part = 0; Part < Resolved.Shapes.size() && Part < 3; ++Part)
        {
            std::string Shape = Resolved.Shapes[Part];
            if (Shape.rfind("Shape ", 0) == 0) Shape = Shape.substr(6);
            Subtitle += (Part == 0 ? " \xC2\xB7 " : " + ") + Shape;
        }
        char Copies[64];
        if (Resolved.Copies > 1 || !Parameters.Modifiers.empty())
            std::snprintf(Copies, sizeof(Copies), " \xC2\xB7 %u of %u cop%s shown", Resolved.Shown, Resolved.Copies, Resolved.Copies == 1 ? "y" : "ies");
        else std::snprintf(Copies, sizeof(Copies), " \xC2\xB7 %u triangles", Resolved.Facets);
        Subtitle += Copies;
    }
    bool        Changed = false;
    const char* Pill    = NotesOpen ? "Hide notes" : (Parameters.Notes.empty() ? "Add notes" : "Notes");
    if (RecordHeading::Construct(Page, "Heading", Breadcrumb.c_str(), Name.c_str(), Subtitle.c_str(), Pill, NotesOpen ? "X" : "Plus")) NotesOpen = !NotesOpen;
    if (NotesOpen) Changed |= NotesField::Construct(Page, "Notes", Parameters.Notes, NotesOpen);

    // ② the cards
    Changed |= GeometryTransformSection::Construct(Page, Parameters);
    Changed |= GeometryQuickSection::Construct(Page, Parameters);
    Changed |= ModifierStackSection::Construct(Page, Parameters);
    Changed |= GeometryMaterialSection::Construct(Page, Parameters, Name, MaterialSlot);
    bool OpenEditor = false;
    Changed |= GeometryFractureSection::Construct(Page, Parameters, OpenEditor);
    if (OpenEditor) (void)InspectorRuntime::Instance().RequestEditorOpen(Token);
    FooterRow::Construct(Page, "Levelspace record \xC2\xB7 Geometry", "Saved with the level");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

#elif FRONTIER_EDITOR

void GeometryInspector::ConstructEditor(RecordToken) {}
void GeometryInspector::ConstructInspector() {}

#endif

} // namespace Frontier
