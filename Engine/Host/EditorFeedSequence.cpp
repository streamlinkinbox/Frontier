//============================================================================================================================================
//                                                  EDITORFEEDSEQUENCE.CPP
//============================================================================================================================================
// 🧩 The development editor's live feed — FillRoster walks the level's placements into outliner rows, BuildSheet
//    reads the picked row's figures live, and QueryAnimatedSpan finds the run the motion driver owns. Stateless
//    over the level: both builders recompute the same row layout from it, so the sheet can never disagree
//    with the roster.

#include "EditorFeedSequence.h"
#include "../Editor/OutlinerMetadata.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::HostRuntime {
namespace {

// The roster's rows, in order. Folders are virtual (Ordinal = folder index); placement rows carry their
//    placement ordinal; the fly camera is stock (Ordinal unused).
enum class FeedRowKind : uint32_t { Folder, Placement, FlyCamera, CineCamera, PostProcess };
struct FeedRow
{
    FeedRowKind Kind    = FeedRowKind::Folder;
    uint32_t    Ordinal = 0u;
    uint32_t    Depth = 0u;
};

constexpr uint32_t kFolderCameras     = 0u;
constexpr uint32_t kFolderLighting    = 1u;
constexpr uint32_t kFolderObjects     = 2u;
constexpr uint32_t kFolderRoom        = 3u;
constexpr const char* kFolderLabels[] = { "Scene", "Lighting", "World", "Room" };

constexpr float kFolderTint[3]      = { 0.788f, 0.635f, 0.294f };   // amber, shared by every folder
constexpr float kLightTint[3]       = { 0.961f, 0.827f, 0.294f };   // the lamp rows
constexpr float kCameraTint[3]      = { 0.412f, 0.765f, 1.000f };   // the camera rows
constexpr float kPostProcessTint[3] = { 1.000f, 0.541f, 0.396f };   // post process accent

// Editor.jsx OutlinerMetadata(): every row carries the small live figure under its name. The reference
//    gives one to all eighteen panels, so the feed answers for every family it seats, not only the ones
//    that happened to have a figure to hand.
RowReading PlacementReading(const PlacementRecord& Placement, const SceneStructure& Level) noexcept
{
    RowReading Reading;
    Reading.Position[0] = Placement.WorldTransform[12];
    Reading.Position[1] = Placement.WorldTransform[13];
    Reading.Position[2] = Placement.WorldTransform[14];
    const auto& Luminaires = Level.QueryPunctualLuminaires();
    if (Placement.Luminaire < Luminaires.size())
    {
        const PunctualLuminaireRecord& Lamp = Luminaires[Placement.Luminaire];
        Reading.Referenced = true;
        Reading.Output     = Lamp.Intensity;
        switch (Lamp.Category)
        {
        case PunctualLuminaireCategory::Point:     Reading.Shape = LuminaireShape::Point; break;
        case PunctualLuminaireCategory::Spot:      Reading.Shape = LuminaireShape::Spot;  break;
        case PunctualLuminaireCategory::Tube:      Reading.Shape = LuminaireShape::Tube;  break;
        case PunctualLuminaireCategory::Strip:     Reading.Shape = LuminaireShape::Strip;
                                                   Reading.Output = Lamp.LumensPerMetre * Lamp.Size[0] * Lamp.Dimmer;
                                                   break;
        case PunctualLuminaireCategory::Rectangle: Reading.Shape = LuminaireShape::Area;  break;
        default:
            // A directional source is not one of the seven reference shapes; it reports lux, as an
            //    unreferenced row does, and names itself Area the way the browser's default does.
            Reading.Referenced = false;
            Reading.Intensity  = Lamp.Intensity;
            Reading.Shape      = LuminaireShape::Area;
            break;
        }
    }
    return Reading;
}

// True only when all of the placement's instances sit on emissive materials. Reads the flattened records —
//    emission_luminance × emission_color — so the test matches what the kernel lights from.
bool PlacementEmits(const PlacementRecord& P, const SceneStructure& Level) noexcept
{
    const auto& Instances = Level.QueryInstances();
    const auto& Records   = Level.QueryMaterials().QueryRecords();
    if (P.FirstInstance >= Instances.size()
        || P.InstanceCount > Instances.size() - P.FirstInstance)
        return false;
    if (P.InstanceCount == 0u) return false;
    for (uint32_t I = 0u; I < P.InstanceCount; ++I)
    {
        const uint32_t Slot = Instances[P.FirstInstance + I].MaterialIndex;
        if (Slot < Records.size())
        {
            const MaterialRecord& R = Records[Slot];
            if (R.EmissiveR + R.EmissiveG + R.EmissiveB <= 0.0f)
                return false;
        }
        else return false;
    }
    return true;
}

// Each placement belongs to exactly one folder: file cameras under Cameras, emissive and luminaire carriers
//    under Lighting, the flagged dynamics under Objects, and everything else — the static scenery — under Room.
//    Vector order within a folder, so Room reads in build order and Objects in body order.
uint32_t PlacementFolder(const PlacementRecord& P, const SceneStructure& Level) noexcept
{
    if (P.Camera != kPlacementNone)
        return kFolderCameras;
    if (P.Dynamic) return kFolderObjects;
    if (PlacementEmits(P, Level)
        || (P.Luminaire != kPlacementNone && P.Luminaire < Level.QueryPunctualLuminaires().size()))
        return kFolderLighting;
    return P.Dynamic ? kFolderObjects : kFolderRoom;
}

// The shared row layout: Cameras -> Lighting -> Objects -> Room (scenery/plinths).
//    Both builders run this, so the sheet's row means what the roster showed.
uint32_t BuildLayout(const SceneStructure& Level, FeedRow* Layout, uint32_t Capacity) noexcept
{
    const auto& Placements = Level.QueryPlacements();
    uint32_t Rows = 0u;
    auto Push = [&](FeedRowKind Kind, uint32_t Ordinal)
    {
        if (Rows < Capacity) { Layout[Rows].Kind = Kind; Layout[Rows].Ordinal = Ordinal; Layout[Rows].Depth = 0; ++Rows; }
    };

    std::vector<bool> Written(Placements.size(), false);
    // 📝 Adjacency and traversal both live on the heap. No recursion per scene depth,
    // and no repeated full-scene scan for every nested placement.
    std::vector<uint32_t> First(Placements.size(), kPlacementNone);
    std::vector<uint32_t> Next(Placements.size(), kPlacementNone);
    for (uint32_t Index = 0; Index < Placements.size(); ++Index)
    {
        const uint32_t Enclosing = Placements[Index].Ancestor;
        if (Enclosing < Placements.size())
        {
            Next[Index] = First[Enclosing];
            First[Enclosing] = Index;
        }
    }
    std::vector<std::pair<uint32_t, uint32_t>> Pending;
    const auto EmitPlacement = [&](uint32_t Slot)
    {
        Pending.clear();
        Pending.emplace_back(Slot, 1u);
        while (!Pending.empty() && Rows < Capacity)
        {
            const auto Current = Pending.back();
            Pending.pop_back();
            if (Current.first >= Placements.size() || Written[Current.first]) continue;
            Written[Current.first] = true;
            Push(FeedRowKind::Placement, Current.first);
            Layout[Rows - 1].Depth = Current.second;
            for (uint32_t Nested = First[Current.first]; Nested != kPlacementNone; Nested = Next[Nested])
                if (!Written[Nested]) Pending.emplace_back(Nested, Current.second + 1);
        }
    };
    for (uint32_t Folder = kFolderCameras; Folder <= kFolderRoom; ++Folder)
    {
        Push(FeedRowKind::Folder, Folder);
        if (Folder == kFolderCameras)
        {
            Push(FeedRowKind::FlyCamera, 0u);
            Push(FeedRowKind::CineCamera, 0u);
            Push(FeedRowKind::PostProcess, 0u);
        }
        for (uint32_t Slot = 0u; Slot < Placements.size(); ++Slot)
            if (Placements[Slot].Ancestor >= Placements.size() && PlacementFolder(Placements[Slot], Level) == Folder)
                EmitPlacement(Slot);
    }
    // Broken/cyclic links remain selectable, rather than disappearing from the roster.
    for (uint32_t Slot = 0u; Slot < Placements.size(); ++Slot)
        if (!Written[Slot]) EmitPlacement(Slot);

    return Rows;
}

void CopyTint(float Tint[3], const float Source[3]) noexcept
{
    Tint[0] = Source[0]; Tint[1] = Source[1]; Tint[2] = Source[2];
}

EditorPropertyGroup& OpenGroup(EditorSheet* Sheet, const char* Title) noexcept
{
    EditorPropertyGroup& Group = Sheet->Groups[Sheet->GroupCount++];
    std::snprintf(Group.Title, sizeof(Group.Title), "%s", Title);
    Group.PropertyCount = 0u;
    return Group;
}

EditorProperty& OpenProp(EditorPropertyGroup& Group, const char* Label,
                         EditorPropertyCategory Category) noexcept
{
    EditorProperty& Prop = Group.Properties[Group.PropertyCount++];
    std::snprintf(Prop.Label, sizeof(Prop.Label), "%s", Label);
    Prop.Category = Category;
    return Prop;
}

// The flat triangles are engine-space (Finalise converts them); the instance worlds carry the file-to-engine
//    swap on top of the node transform, so applying a world to a flat triangle converts TWICE. What the sheet
//    wants is the MOTION since the bake: Delta = Live · Baked⁻¹, identity for untouched rows. All our matrices
//    are rigid, so the inverse is a transpose, exact to float dust.
void InvertRigid(const float* M, float Inv[16]) noexcept
{
    Inv[0] = M[0]; Inv[1] = M[4]; Inv[2] = M[8];  Inv[3] = 0.0f;
    Inv[4] = M[1]; Inv[5] = M[5]; Inv[6] = M[9];  Inv[7] = 0.0f;
    Inv[8] = M[2]; Inv[9] = M[6]; Inv[10] = M[10]; Inv[11] = 0.0f;
    Inv[12] = -(M[0] * M[12] + M[1] * M[13] + M[2] * M[14]);
    Inv[13] = -(M[4] * M[12] + M[5] * M[13] + M[6] * M[14]);
    Inv[14] = -(M[8] * M[12] + M[9] * M[13] + M[10] * M[14]);
    Inv[15] = 1.0f;
}

void MultiplyMatrix(const float* A, const float* B, float C[16]) noexcept
{
    for (uint32_t J = 0u; J < 4u; ++J)
        for (uint32_t I = 0u; I < 4u; ++I)
            C[I + 4u * J] = A[I] * B[4u * J] + A[I + 4u] * B[1u + 4u * J]
                + A[I + 8u] * B[2u + 4u * J] + A[I + 12u] * B[3u + 4u * J];
}

// The effective world matrix of one instance: the live row when it differs from the level's, the level's
//    otherwise. Orientation only — positions go through the motion delta below, never through this.
const float* EffectiveWorld(uint32_t Instance, const SceneStructure& Level,
                            const std::vector<InstanceRecord>& Live) noexcept
{
    const auto& Baked = Level.QueryInstances();
    if (Instance < Baked.size() && Instance < Live.size()
        && std::memcmp(Live[Instance].World, Baked[Instance].World, sizeof(float) * 16u) != 0)
        return Live[Instance].World;
    return Instance < Baked.size() ? Baked[Instance].World : nullptr;
}

void MotionDelta(uint32_t Instance, const SceneStructure& Level,
                 const std::vector<InstanceRecord>& Live, float Delta[16]) noexcept
{
    static constexpr float kIdentity[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                             0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    const auto& Baked = Level.QueryInstances();
    std::memcpy(Delta, kIdentity, sizeof(float) * 16u);
    if (Instance >= Baked.size() || Instance >= Live.size()
        || std::memcmp(Live[Instance].World, Baked[Instance].World, sizeof(float) * 16u) == 0)
        return;
    float Inv[16];
    InvertRigid(Baked[Instance].World, Inv);
    MultiplyMatrix(Live[Instance].World, Inv, Delta);
}

void TransformPoint(const float* M, const float P[3], float Out[3]) noexcept
{
    Out[0] = M[0] * P[0] + M[4] * P[1] + M[8] * P[2] + M[12];
    Out[1] = M[1] * P[0] + M[5] * P[1] + M[9] * P[2] + M[13];
    Out[2] = M[2] * P[0] + M[6] * P[1] + M[10] * P[2] + M[14];
}

// Area-weighted centroid and geometric normal of the placement's engine-space triangles, carried by the motion
//    delta. Untouched rows average the bake, bit for bit; driven bodies show where they ARE. Empty placements
//    (no instances, no triangles) fall back to the placement's own translation.
void MeasurePlacement(const PlacementRecord& P, const SceneStructure& Level,
                      const std::vector<InstanceRecord>& Live, float Centroid[3], float Normal[3]) noexcept
{
    Centroid[0] = P.WorldTransform[12]; Centroid[1] = P.WorldTransform[13]; Centroid[2] = P.WorldTransform[14];
    Normal[0] = 0.0f; Normal[1] = 0.0f; Normal[2] = 1.0f;
    const auto& Instances = Level.QueryInstances();
    const auto& Flat      = Level.QueryFlatTriangles();
    if (P.FirstInstance >= Instances.size() || P.InstanceCount > Instances.size() - P.FirstInstance)
        return;
    double Cx = 0.0, Cy = 0.0, Cz = 0.0, Nx = 0.0, Ny = 0.0, Nz = 0.0, Area = 0.0;
    for (uint32_t I = 0u; I < P.InstanceCount; ++I)
    {
        const uint32_t      Ordinal = P.FirstInstance + I;
        const InstanceRecord& Baked = Instances[Ordinal];
        if (Baked.FlatTriangleOffset >= Flat.size()
            || Baked.TriangleCount > Flat.size() - Baked.FlatTriangleOffset)
            continue;
        float M[16];
        MotionDelta(Ordinal, Level, Live, M);
        for (uint32_t T = 0u; T < Baked.TriangleCount; ++T)
        {
            const TriangleIndex& Tri = Flat[Baked.FlatTriangleOffset + T];
            const float A[3] = { Tri.VertexAlphaX, Tri.VertexAlphaY, Tri.VertexAlphaZ };
            const float B[3] = { Tri.VertexBetaX, Tri.VertexBetaY, Tri.VertexBetaZ };
            const float C[3] = { Tri.VertexGammaX, Tri.VertexGammaY, Tri.VertexGammaZ };
            float a[3], b[3], c[3];
            TransformPoint(M, A, a); TransformPoint(M, B, b); TransformPoint(M, C, c);
            const float Ux = b[0] - a[0], Uy = b[1] - a[1], Uz = b[2] - a[2];
            const float Vx = c[0] - a[0], Vy = c[1] - a[1], Vz = c[2] - a[2];
            const float Wx = Uy * Vz - Uz * Vy, Wy = Uz * Vx - Ux * Vz, Wz = Ux * Vy - Uy * Vx;
            const double W = std::sqrt(static_cast<double>(Wx * Wx + Wy * Wy + Wz * Wz));
            if (W <= 0.0)
                continue;
            Cx += (a[0] + b[0] + c[0]) / 3.0 * W; Cy += (a[1] + b[1] + c[1]) / 3.0 * W; Cz += (a[2] + b[2] + c[2]) / 3.0 * W;
            Nx += Wx; Ny += Wy; Nz += Wz;
            Area += W;
        }
    }
    if (Area <= 0.0)
        return;
    Centroid[0] = static_cast<float>(Cx / Area); Centroid[1] = static_cast<float>(Cy / Area); Centroid[2] = static_cast<float>(Cz / Area);
    const double N = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
    if (N > 0.0) { Normal[0] = static_cast<float>(Nx / N); Normal[1] = static_cast<float>(Ny / N); Normal[2] = static_cast<float>(Nz / N); }
}

// The emission direction snapped to its dominant world axis — "-Z (nadir)" for a ceiling luminaire.
void FormatDirection(char Text[48], const float D[3]) noexcept
{
    const float Ax = std::fabs(D[0]), Ay = std::fabs(D[1]), Az = std::fabs(D[2]);
    const char* Face = "—";
    if (Az >= Ax && Az >= Ay)      Face = D[2] >= 0.0f ? "+Z (zenith)" : "-Z (nadir)";
    else if (Ay >= Ax)             Face = D[1] >= 0.0f ? "+Y (north)" : "-Y (south)";
    else                           Face = D[0] >= 0.0f ? "+X (east)" : "-X (west)";
    std::snprintf(Text, 48u, "%s", Face);
}

} // namespace

uint32_t EditorFeedSequence::FillRoster(EditorInstance* Instances, const SceneStructure& Level,
                                        uint32_t Capacity) const noexcept
{
    if (Instances == nullptr || Capacity == 0u)
        return 0u;
    const uint32_t TargetCapacity = std::min(Capacity, kMaxEditorInstances);
    std::vector<FeedRow> Layout(TargetCapacity);
    const uint32_t Rows = BuildLayout(Level, Layout.data(), TargetCapacity);
    const auto& Placements = Level.QueryPlacements();
    const auto& LevelInstances = Level.QueryInstances();
    const auto& Records = Level.QueryMaterials().QueryRecords();

    for (uint32_t R = 0u; R < Rows && R < TargetCapacity; ++R)
    {
        EditorInstance& Row = Instances[R];
        Row = EditorInstance{};
        const FeedRow& Entry = Layout[R];
        // Stable across roster refresh/insertion so session-owned Notes can follow the actual scene row.
        Row.InspectorKey=(Entry.Kind==FeedRowKind::Placement?0x200000000ull:Entry.Kind==FeedRowKind::Folder?0x100000000ull:0x300000000ull)+Entry.Ordinal+1;
        Row.Visible = true;
        Row.Locked  = false;
        Row.Solo    = false;
        Row.Physics = false;
        switch (Entry.Kind)
        {
        case FeedRowKind::Folder:
            std::snprintf(Row.Label, sizeof(Row.Label), "%s", kFolderLabels[Entry.Ordinal]);
            Row.Depth    = 0u;
            Row.Category = EditorInstanceCategory::Folder;
            Row.Glyph = EditorGlyph::Folder;
            Row.Artwork = Entry.Ordinal==kFolderCameras?IconSymbol::FolderScene:
                (Entry.Ordinal==kFolderObjects||Entry.Ordinal==kFolderRoom)?IconSymbol::FolderWorld:IconSymbol::FolderGeneric;
            Row.Shut     = (Entry.Ordinal == kFolderRoom); // Scenery/Plinths folder is shut by default so it doesn't flood the outliner
            CopyTint(Row.Tint, kFolderTint);
            break;
        case FeedRowKind::Placement:
        {
            const PlacementRecord& P = Placements[Entry.Ordinal];
            if (!P.Name.empty()) std::snprintf(Row.Label, sizeof(Row.Label), "%s", P.Name.c_str());
            else                 std::snprintf(Row.Label, sizeof(Row.Label), "Object %u", Entry.Ordinal);
            Row.Depth = Entry.Depth;
            Row.Dynamic = P.Dynamic;
            const RowReading Reading = PlacementReading(P, Level);
            const uint32_t Folder = PlacementFolder(P, Level);
            if (P.InstanceCount == 0u && P.Camera == kPlacementNone && P.Luminaire == kPlacementNone)
            {
                Row.Category = EditorInstanceCategory::Folder;
                Row.Glyph = EditorGlyph::Folder;
                CopyTint(Row.Tint, kFolderTint);
            }
            else if (Folder == kFolderLighting)
            {
                Row.Category = EditorInstanceCategory::Light;
                CopyTint(Row.Tint, kLightTint);
                if(P.Luminaire<Level.QueryPunctualLuminaires().size()){
                    const auto& L=Level.QueryPunctualLuminaires()[P.Luminaire];
                    OutlinerMetadata(RowPanel::Light,Reading,Row.Meta,sizeof(Row.Meta));std::snprintf(Row.Tag,sizeof(Row.Tag),"LGT");Row.Standing=L.Enabled?EditorStanding::Ok:EditorStanding::Quiet;std::snprintf(Row.StandingNote,sizeof(Row.StandingNote),L.Enabled?"Enabled":"Disabled");
                }
            }
            else if (Folder == kFolderCameras)
            {
                Row.Category = EditorInstanceCategory::Camera;
                OutlinerMetadata(RowPanel::Camera, Reading, Row.Meta, sizeof(Row.Meta));
                CopyTint(Row.Tint, kCameraTint);
            }
            else
            {
                Row.Category = EditorInstanceCategory::Geometry;
                OutlinerMetadata(RowPanel::Geometry, Reading, Row.Meta, sizeof(Row.Meta));
                const uint32_t First = P.FirstInstance < LevelInstances.size() ? P.FirstInstance : 0u;
                const uint32_t Slot = (P.InstanceCount > 0u && First < LevelInstances.size())
                    ? LevelInstances[First].MaterialIndex : 0xFFFFFFFFu;
                if (Slot < Records.size())
                {
                    Row.Tint[0] = Records[Slot].AlbedoR;
                    Row.Tint[1] = Records[Slot].AlbedoG;
                    Row.Tint[2] = Records[Slot].AlbedoB;
                }
            }
            break;
        }
        case FeedRowKind::FlyCamera:
            std::snprintf(Row.Label, sizeof(Row.Label), "Editor Camera");
            Row.Depth    = 1u;
            Row.Pinned = true;Row.Locked = true;
            Row.Category = EditorInstanceCategory::Camera;
            Row.Glyph    = EditorGlyph::Camera;Row.Artwork=IconSymbol::Camera;
            Row.Narrowing = EditorNarrowing::Camera;
            OutlinerMetadata(RowPanel::EditorCamera, RowReading{}, Row.Meta, sizeof(Row.Meta));
            CopyTint(Row.Tint, kCameraTint);
            break;
        case FeedRowKind::CineCamera:
            std::snprintf(Row.Label, sizeof(Row.Label), "Cine Camera");
            Row.Depth    = 1u;
            Row.Category = EditorInstanceCategory::Camera;
            Row.Glyph    = EditorGlyph::Camera;Row.Artwork=IconSymbol::Camera;
            Row.Narrowing = EditorNarrowing::Camera;
            OutlinerMetadata(RowPanel::Camera, RowReading{}, Row.Meta, sizeof(Row.Meta));
            CopyTint(Row.Tint, kCameraTint);
            break;
        case FeedRowKind::PostProcess:
            std::snprintf(Row.Label, sizeof(Row.Label), "Post Process");
            Row.Depth    = 1u;
            Row.Category = EditorInstanceCategory::Geometry;
            Row.Glyph    = EditorGlyph::Effects;
            Row.Narrowing = EditorNarrowing::Camera;
            std::snprintf(Row.Tag, sizeof(Row.Tag), "Comp");
            OutlinerMetadata(RowPanel::Post, RowReading{}, Row.Meta, sizeof(Row.Meta));
            CopyTint(Row.Tint, kPostProcessTint);
            break;
        }
    }
    // Folders count their direct rows so the panel can badge them.
    for (uint32_t R = 0u; R < Rows; ++R)
    {
        if (Layout[R].Kind != FeedRowKind::Folder)
            continue;
        uint32_t Kids = 0u;
        for (uint32_t K = R + 1u; K < Rows && Instances[K].Depth > 0u; ++K)
            if (Instances[K].Depth == 1u)
                ++Kids;
        Instances[R].KidCount = Kids;
        RowReading Enclosing;
        Enclosing.Enclosed = Kids;
        OutlinerMetadata(RowPanel::Group, Enclosing, Instances[R].Meta, sizeof(Instances[R].Meta));
    }
    return Rows;
}


void QueryLevelCentre(const SceneStructure& Level, float Centre[3]) noexcept
{
    if (Centre == nullptr)
        return;
    Centre[0] = 0.0f; Centre[1] = 0.0f; Centre[2] = 0.0f;
    const auto& Flat = Level.QueryFlatTriangles();
    if (!Flat.empty())
    {
        // The bounds' midpoint, not the vertex mean: a dense floor grid must not drag the middle down.
        float Lo[3] = { Flat[0].VertexAlphaX, Flat[0].VertexAlphaY, Flat[0].VertexAlphaZ };
        float Hi[3] = { Lo[0], Lo[1], Lo[2] };
        for (const TriangleIndex& T : Flat)
        {
            const float Vx[3] = { T.VertexAlphaX, T.VertexBetaX, T.VertexGammaX };
            const float Vy[3] = { T.VertexAlphaY, T.VertexBetaY, T.VertexGammaY };
            const float Vz[3] = { T.VertexAlphaZ, T.VertexBetaZ, T.VertexGammaZ };
            for (int K = 0; K < 3; ++K)
            {
                if (Vx[K] < Lo[0]) Lo[0] = Vx[K];
                if (Vx[K] > Hi[0]) Hi[0] = Vx[K];
                if (Vy[K] < Lo[1]) Lo[1] = Vy[K];
                if (Vy[K] > Hi[1]) Hi[1] = Vy[K];
                if (Vz[K] < Lo[2]) Lo[2] = Vz[K];
                if (Vz[K] > Hi[2]) Hi[2] = Vz[K];
            }
        }
        Centre[0] = (Lo[0] + Hi[0]) * 0.5f;
        Centre[1] = (Lo[1] + Hi[1]) * 0.5f;
        Centre[2] = (Lo[2] + Hi[2]) * 0.5f;
        return;
    }
    const auto& Placements = Level.QueryPlacements();
    if (Placements.empty())
        return;
    double X = 0.0, Y = 0.0, Z = 0.0;
    for (const PlacementRecord& P : Placements)
    {
        X += P.WorldTransform[12];
        Y += P.WorldTransform[13];
        Z += P.WorldTransform[14];
    }
    Centre[0] = static_cast<float>(X / Placements.size());
    Centre[1] = static_cast<float>(Y / Placements.size());
    Centre[2] = static_cast<float>(Z / Placements.size());
}

EditorProperty* EditorFeedSequence::BuildSheet(uint32_t Index, EditorInstance* Instances, uint32_t RowCount,
                                              EditorSheet* Sheet, const FlyThroughSolver& Camera,
                                              const SceneStructure& Level,
                                              const std::vector<InstanceRecord>& Live) const noexcept
{
    if (Sheet == nullptr)
        return nullptr;
    Sheet->Appearance=EditorSheetAppearance::Generic;Sheet->CameraLive=false;Sheet->CameraAspect=1.5f;
    Sheet->GroupCount = 0u;
    if (Instances == nullptr || Index >= RowCount || RowCount > kMaxEditorInstances)
        return nullptr;

    using Frontier::EditorPropertyCategory;
    constexpr float kRadToDeg = 57.29578f;
    std::vector<FeedRow> Layout(RowCount);
    const uint32_t StockRows = BuildLayout(Level, Layout.data(), RowCount);
    const uint32_t SourceIndex=(Instances[Index].InspectorKey>>32)==1?uint32_t(Instances[Index].InspectorKey)-1:Index;
    if (SourceIndex >= StockRows)
        return nullptr;
    const FeedRow& Picked = Layout[SourceIndex];
    EditorProperty* TintMirror = nullptr;

    if (Picked.Kind == FeedRowKind::Folder)
    {
        EditorPropertyGroup& Group = OpenGroup(Sheet, "Group");
        uint32_t Total = 0u;
        for (uint32_t J = Index + 1u; J < RowCount && Instances[J].Depth > Instances[Index].Depth; ++J)
            ++Total;
        EditorProperty& Contents = OpenProp(Group, "Contents", EditorPropertyCategory::Readout);
        std::snprintf(Contents.Text, sizeof(Contents.Text), "%u direct \xc2\xb7 %u total",
                      Instances[Index].KidCount, Total);
        EditorProperty& Tint = OpenProp(Group, "Tint", EditorPropertyCategory::Colour);
        Tint.ColourTint[0] = Instances[Index].Tint[0];
        Tint.ColourTint[1] = Instances[Index].Tint[1];
        Tint.ColourTint[2] = Instances[Index].Tint[2];
        Tint.Swatches = true;
        TintMirror = &Tint;
        return TintMirror;
    }

    if (Picked.Kind == FeedRowKind::Placement)
    {
        const auto& Placements = Level.QueryPlacements();
        const auto& Records    = Level.QueryMaterials().QueryRecords();
        const PlacementRecord& P = Placements[Picked.Ordinal];
        const uint32_t Folder = PlacementFolder(P, Level);
        const auto& LevelInstances = Level.QueryInstances();
        const uint32_t Slot = (P.InstanceCount > 0u && P.FirstInstance < LevelInstances.size())
            ? LevelInstances[P.FirstInstance].MaterialIndex : 0xFFFFFFFFu;
        const MaterialRecord* Mat = Slot < Records.size() ? &Records[Slot] : nullptr;

        if (Folder == kFolderCameras)
        {
            EditorPropertyGroup& Placed = OpenGroup(Sheet, "Transform");
            EditorProperty& Where = OpenProp(Placed, "Position", EditorPropertyCategory::AxisVec3);
            Where.Axes[0] = P.WorldTransform[12];
            Where.Axes[1] = P.WorldTransform[13];
            Where.Axes[2] = P.WorldTransform[14];
            Where.AxisStep = 0.05f;
            Where.Editable = false;
            EditorProperty& Source = OpenProp(Placed, "Source", EditorPropertyCategory::Readout);
            std::snprintf(Source.Text, sizeof(Source.Text), "File");
            return nullptr;
        }

        if (Folder == kFolderLighting)
        {
            float Centroid[3], Normal[3];
            MeasurePlacement(P, Level, Live, Centroid, Normal);
            const auto& Punctuals = Level.QueryPunctualLuminaires();
            const bool HasPunctual = P.Luminaire != kPlacementNone && P.Luminaire < Punctuals.size();
            if (HasPunctual)
            {
                Sheet->Appearance=EditorSheetAppearance::Light;
                const PunctualLuminaireRecord& L=Punctuals[P.Luminaire];
                EditorPropertyGroup& Source=OpenGroup(Sheet,"Light source");
                auto& Enabled=OpenProp(Source,"Enabled",EditorPropertyCategory::Switch);Enabled.On=L.Enabled;
                auto& Shadows=OpenProp(Source,"Cast Shadows",EditorPropertyCategory::Switch);Shadows.On=L.CastShadows;
                if(L.Category==PunctualLuminaireCategory::Strip){auto& Emitter=OpenProp(Source,"Draw emitter",EditorPropertyCategory::Switch);Emitter.On=L.DrawEmitter;}
                auto& Type=OpenProp(Source,"Type",EditorPropertyCategory::Select);Type.OptionCount=6;Type.Picked=static_cast<uint32_t>(L.Category);
                const char* Types[]={"Directional","Point","Spot","Rectangle / Area","Tube","Strip"};for(unsigned I=0;I<6;++I)std::snprintf(Type.Options[I],sizeof(Type.Options[I]),"%s",Types[I]);
                auto& Power=OpenProp(Source,"Intensity",EditorPropertyCategory::Slider);Power.Minimum=0;Power.Maximum=L.Category==PunctualLuminaireCategory::Directional?200000.f:100000.f;Power.Figure=L.Category==PunctualLuminaireCategory::Strip?L.LumensPerMetre*L.Size[0]*L.Dimmer:L.Intensity;Power.Decimals=1;std::snprintf(Power.Unit,sizeof(Power.Unit),L.Category==PunctualLuminaireCategory::Directional?"lx":L.Category==PunctualLuminaireCategory::Strip?"lm":"cd");
                auto& Hue=OpenProp(Source,"Colour",EditorPropertyCategory::Colour);CopyTint(Hue.ColourTint,L.Colour);
                auto& Range=OpenProp(Source,"Range",EditorPropertyCategory::Slider);Range.Minimum=0;Range.Maximum=1000;Range.Figure=L.Range;Range.Decimals=1;std::snprintf(Range.Unit,sizeof(Range.Unit),"m");
                EditorPropertyGroup& Transform=OpenGroup(Sheet,"Transform");
                auto& Position=OpenProp(Transform,"Position",EditorPropertyCategory::AxisVec3);for(int I=0;I<3;++I)Position.Axes[I]=P.WorldTransform[12+I];Position.AxisStep=.05f;Position.Editable=true;
                // Rotation and scale are recovered from the placement basis so the Transform grid edits
                // the same matrix the gizmo does rather than a parallel copy.
                auto& Turn=OpenProp(Transform,"Rotation",EditorPropertyCategory::AxisVec3);Turn.AxisStep=1;Turn.Editable=true;
                auto& Size=OpenProp(Transform,"Scale",EditorPropertyCategory::AxisVec3);Size.AxisStep=.01f;Size.Editable=true;
                {
                    const float* M=P.WorldTransform;
                    float Basis[3][3];for(int C=0;C<3;++C)for(int R=0;R<3;++R)Basis[C][R]=M[C*4+R];
                    for(int C=0;C<3;++C){Size.Axes[C]=std::sqrt(Basis[C][0]*Basis[C][0]+Basis[C][1]*Basis[C][1]+Basis[C][2]*Basis[C][2]);
                        const float Length=Size.Axes[C]>1e-6f?Size.Axes[C]:1.f;for(int R=0;R<3;++R)Basis[C][R]/=Length;}
                    Turn.Axes[1]=std::atan2(-Basis[2][0],Basis[2][2])*kRadToDeg;
                    Turn.Axes[0]=std::asin(std::clamp(Basis[2][1],-1.f,1.f))*kRadToDeg;
                    Turn.Axes[2]=std::atan2(-Basis[0][1],Basis[1][1])*kRadToDeg;
                }
                auto& Direction=OpenProp(Transform,"Direction",EditorPropertyCategory::Readout);const float Down[3]={-P.WorldTransform[8],-P.WorldTransform[9],-P.WorldTransform[10]};FormatDirection(Direction.Text,Down);
                EditorPropertyGroup& Shape=OpenGroup(Sheet,"Distribution");
                auto& Distribution=OpenProp(Shape,"Distribution",EditorPropertyCategory::Select);Distribution.OptionCount=3;Distribution.Picked=static_cast<uint32_t>(L.Distribution);const char* Distributions[]={"Uniform","IES profile","ECE low beam"};for(unsigned I=0;I<3;++I)std::snprintf(Distribution.Options[I],sizeof(Distribution.Options[I]),"%s",Distributions[I]);
                auto Slider=[&](const char* Name,float Value,float Min,float Max,const char* Unit){auto& Q=OpenProp(Shape,Name,EditorPropertyCategory::Slider);Q.Minimum=Min;Q.Maximum=Max;Q.Figure=Value;Q.Decimals=2;std::snprintf(Q.Unit,sizeof(Q.Unit),"%s",Unit);};
                Slider("Inner Cone",L.InnerConeAngle*kRadToDeg,0,89,"deg");Slider("Outer Cone",L.OuterConeAngle*kRadToDeg,1,90,"deg");Slider("Width",L.Size[0],.01f,100,"m");Slider("Height",L.Size[1],.01f,100,"m");
                // Emitter flux, colour temperature and the shadow/response trio drive the approved
                // Luminous output, Colour temperature and Shadows & response cards for every category.
                EditorPropertyGroup& Emission=OpenGroup(Sheet,"Emission");
                auto Measure=[&](EditorPropertyGroup& Group,const char* Name,float Value,float Min,float Max,uint32_t Decimals,const char* Unit){auto& Q=OpenProp(Group,Name,EditorPropertyCategory::Slider);Q.Minimum=Min;Q.Maximum=Max;Q.Figure=Value;Q.Decimals=Decimals;std::snprintf(Q.Unit,sizeof(Q.Unit),"%s",Unit);};
                const bool Linear=L.Category==PunctualLuminaireCategory::Strip;
                Measure(Emission,"Luminous flux",Linear?L.LumensPerMetre*L.Size[0]*L.Dimmer:L.Intensity,0,20000,0,"lm");
                Measure(Emission,"Colour temperature",L.Temperature,1800,12000,0,"K");
                Measure(Emission,"Shadow softness",L.ShadowSoftness,0,100,0,"%");
                Measure(Emission,"Diffuse response",L.DiffuseResponse,0,200,0,"%");
                Measure(Emission,"Specular response",L.SpecularResponse,0,200,0,"%");
                // Aim is owned by the placement transform, so it is reported rather than re-authored here.
                auto& Azimuth=OpenProp(Emission,"Aim azimuth",EditorPropertyCategory::Readout);
                auto& Elevation=OpenProp(Emission,"Aim elevation",EditorPropertyCategory::Readout);
                const float Forward[3]={-P.WorldTransform[8],-P.WorldTransform[9],-P.WorldTransform[10]};
                std::snprintf(Azimuth.Text,sizeof(Azimuth.Text),"%.0f deg",double(std::atan2(Forward[0],-Forward[2])*kRadToDeg));
                std::snprintf(Elevation.Text,sizeof(Elevation.Text),"%.0f deg",double(std::asin(std::clamp(Forward[1],-1.f,1.f))*kRadToDeg));
                if(L.Category==PunctualLuminaireCategory::Strip){
                    EditorPropertyGroup& Output=OpenGroup(Sheet,"Output per metre");
                    auto StripSlider=[&](EditorPropertyGroup& Group,const char* Name,float Value,float Min,float Max,uint32_t Decimals,const char* Unit){auto& Q=OpenProp(Group,Name,EditorPropertyCategory::Slider);Q.Minimum=Min;Q.Maximum=Max;Q.Figure=Value;Q.Decimals=Decimals;std::snprintf(Q.Unit,sizeof(Q.Unit),"%s",Unit);};
                    StripSlider(Output,"Flux per metre",L.LumensPerMetre,10,4000,0,"lm/m");StripSlider(Output,"Load per metre",L.WattsPerMetre,1,50,1,"W/m");StripSlider(Output,"Dimmer",L.Dimmer,0,1,2,"");StripSlider(Output,"Colour temperature",L.Temperature,1800,12000,0,"K");
                    EditorPropertyGroup& Layout=OpenGroup(Sheet,"Layout & segments");
                    StripSlider(Layout,"Strip length",L.Size[0],.1f,20,1,"m");StripSlider(Layout,"Emitter density",L.EmittersPerMetre,10,240,0,"/m");StripSlider(Layout,"Supply voltage",L.SupplyVoltage,5,48,0,"V");auto& Diffuser=OpenProp(Layout,"Opal diffuser",EditorPropertyCategory::Switch);Diffuser.On=L.Diffuser;
                }
                return nullptr;
            }
            EditorPropertyGroup& Lamp = OpenGroup(Sheet, "Light");
            EditorProperty& Power = OpenProp(Lamp, "Intensity", EditorPropertyCategory::Slider);
            EditorProperty& Hue = OpenProp(Lamp, "Colour", EditorPropertyCategory::Colour);
            if (Mat != nullptr && (Mat->EmissiveR + Mat->EmissiveG + Mat->EmissiveB) > 0.0f)
            {
                const float Brightest = std::max({Mat->EmissiveR,Mat->EmissiveG,Mat->EmissiveB});
                Power.Minimum=0;Power.Maximum=64;Power.Figure=Mat->EmissiveR;Power.Decimals=1;std::snprintf(Power.Unit,sizeof(Power.Unit),"lx");
                Hue.ColourTint[0]=Brightest>0?Mat->EmissiveR/Brightest:1;Hue.ColourTint[1]=Brightest>0?Mat->EmissiveG/Brightest:1;Hue.ColourTint[2]=Brightest>0?Mat->EmissiveB/Brightest:1;
            }
            EditorPropertyGroup& Aimed=OpenGroup(Sheet,"Aim");auto& Facing=OpenProp(Aimed,"Direction",EditorPropertyCategory::Readout);FormatDirection(Facing.Text,Normal);
            return nullptr;
        }

        float Centroid[3], Normal[3];
        MeasurePlacement(P, Level, Live, Centroid, Normal);
        float ZDegrees = 0.0f;
        if (P.InstanceCount > 0u && P.FirstInstance < LevelInstances.size())
        {
            const float* W = EffectiveWorld(P.FirstInstance, Level, Live);
            if (W != nullptr)
                ZDegrees = std::atan2(W[4], W[0]) * kRadToDeg;
        }
        EditorPropertyGroup& Placed = OpenGroup(Sheet, "Transform");
        EditorProperty& Where = OpenProp(Placed, "Position", EditorPropertyCategory::AxisVec3);
        Where.Axes[0] = Centroid[0];
        Where.Axes[1] = Centroid[1];
        Where.Axes[2] = Centroid[2];
        Where.AxisStep = 0.05f;
        Where.Editable = false;
        EditorProperty& Spin = OpenProp(Placed, "Rotation", EditorPropertyCategory::Readout);
        std::snprintf(Spin.Text, sizeof(Spin.Text), "%+.0f\xc2\xb0 about Z", static_cast<double>(ZDegrees));

        if (Mat != nullptr)
        {
            EditorPropertyGroup& Faced = OpenGroup(Sheet, "Surface");
            EditorProperty& Albedo = OpenProp(Faced, "Albedo", EditorPropertyCategory::Colour);
            Albedo.ColourTint[0] = Mat->AlbedoR;
            Albedo.ColourTint[1] = Mat->AlbedoG;
            Albedo.ColourTint[2] = Mat->AlbedoB;
            EditorProperty& Emitted = OpenProp(Faced, "Emission", EditorPropertyCategory::Slider);
            Emitted.Minimum = 0.0f; Emitted.Maximum = 64.0f; Emitted.Figure = Mat->EmissiveR;
            Emitted.Decimals = 1u;
            std::snprintf(Emitted.Unit, sizeof(Emitted.Unit), "lx");
            EditorProperty& Rough = OpenProp(Faced, "Roughness", EditorPropertyCategory::Slider);
            Rough.Minimum = 0.0f; Rough.Maximum = 1.0f; Rough.Figure = Mat->Roughness;
            Rough.Decimals = 2u;
            EditorProperty& Metal = OpenProp(Faced, "Metallic", EditorPropertyCategory::Readout);
            std::snprintf(Metal.Text, sizeof(Metal.Text), "%.2f", static_cast<double>(Mat->Metalness));
        }
        return nullptr;
    }

    if(Picked.Kind==FeedRowKind::FlyCamera||Picked.Kind==FeedRowKind::CineCamera){
        BuildCameraInspectorSheet(Camera,Picked.Kind==FeedRowKind::FlyCamera?MainLens:CineLens,*Sheet,Picked.Kind==FeedRowKind::FlyCamera);return nullptr;
    }

    if (Picked.Kind == FeedRowKind::PostProcess)
    {
        Sheet->Appearance=EditorSheetAppearance::PostProcess;
        EditorPropertyGroup& Exp = OpenGroup(Sheet, "Exposure");
        EditorProperty& Ev = OpenProp(Exp, "EV Compensation", EditorPropertyCategory::Slider);
        Ev.Minimum = -4.0f; Ev.Maximum = 4.0f; Ev.Figure = PostExposure; Ev.Decimals = 2u;
        std::snprintf(Ev.Unit, sizeof(Ev.Unit), "EV");

        EditorPropertyGroup& Tone = OpenGroup(Sheet, "Tone Mapping");
        EditorProperty& Sat = OpenProp(Tone, "Saturation", EditorPropertyCategory::Slider);
        Sat.Minimum = 0.0f; Sat.Maximum = 2.0f; Sat.Figure = PostSaturation; Sat.Decimals = 2u;
        EditorProperty& Contrast = OpenProp(Tone, "Contrast", EditorPropertyCategory::Slider);
        Contrast.Minimum = 0.5f; Contrast.Maximum = 2.0f; Contrast.Figure = PostContrast; Contrast.Decimals = 2u;

        EditorPropertyGroup& LensFx = OpenGroup(Sheet, "Lens Effects");
        EditorProperty& Bloom = OpenProp(LensFx, "Bloom Intensity", EditorPropertyCategory::Slider);
        Bloom.Minimum = 0.0f; Bloom.Maximum = 1.0f; Bloom.Figure = PostBloom; Bloom.Decimals = 2u;
        EditorProperty& Vig = OpenProp(LensFx, "Vignette", EditorPropertyCategory::Slider);
        Vig.Minimum = 0.0f; Vig.Maximum = 1.0f; Vig.Figure = PostVignette; Vig.Decimals = 2u;
        return nullptr;
    }

    return nullptr;
}

void EditorFeedSequence::ApplyCameraSheet(uint32_t Index,uint32_t RowCount,const SceneStructure& Level,const EditorSheet& Sheet,FlyThroughSolver& Camera) noexcept {
    if(RowCount>kMaxEditorInstances||Index>=RowCount||Sheet.Appearance!=EditorSheetAppearance::Camera)return;
    std::vector<FeedRow> Layout(RowCount);const uint32_t Count=BuildLayout(Level,Layout.data(),RowCount);if(Index>=Count)return;
    if(Layout[Index].Kind==FeedRowKind::FlyCamera)ApplyCameraInspectorSheet(Camera,MainLens,Sheet,true);
    else if(Layout[Index].Kind==FeedRowKind::CineCamera)ApplyCameraInspectorSheet(Camera,CineLens,Sheet,false);
}

void EditorFeedSequence::ApplyPostProcessSheet(const EditorSheet& Sheet) noexcept {
    if(Sheet.Appearance!=EditorSheetAppearance::PostProcess)return;
    auto Find=[&](const char* Name)->const EditorProperty*{for(uint32_t G=0;G<Sheet.GroupCount;++G)for(uint32_t I=0;I<Sheet.Groups[G].PropertyCount;++I)if(!std::strcmp(Sheet.Groups[G].Properties[I].Label,Name))return &Sheet.Groups[G].Properties[I];return nullptr;};
    if(auto* P=Find("EV Compensation"))PostExposure=P->Figure;
    if(auto* P=Find("Saturation"))PostSaturation=P->Figure;
    if(auto* P=Find("Contrast"))PostContrast=P->Figure;
    if(auto* P=Find("Bloom Intensity"))PostBloom=P->Figure;
    if(auto* P=Find("Vignette"))PostVignette=P->Figure;
}

void EditorFeedSequence::ApplyLightSheet(uint32_t Index,uint32_t RowCount,SceneStructure& Level,const EditorSheet& Sheet) noexcept {
    if(RowCount>kMaxEditorInstances||Index>=RowCount||Sheet.Appearance!=EditorSheetAppearance::Light)return;
    std::vector<FeedRow> Layout(RowCount);const uint32_t Count=BuildLayout(Level,Layout.data(),RowCount);if(Index>=Count||Layout[Index].Kind!=FeedRowKind::Placement)return;
    auto& Placements=Level.AccessPlacements();auto& Lights=Level.AccessPunctualLuminaires();const uint32_t PIndex=Layout[Index].Ordinal;if(PIndex>=Placements.size())return;auto& P=Placements[PIndex];if(P.Luminaire>=Lights.size())return;auto& L=Lights[P.Luminaire];
    auto Find=[&](const char* Name)->const EditorProperty*{for(uint32_t G=0;G<Sheet.GroupCount;++G)for(uint32_t I=0;I<Sheet.Groups[G].PropertyCount;++I)if(!std::strcmp(Sheet.Groups[G].Properties[I].Label,Name))return &Sheet.Groups[G].Properties[I];return nullptr;};
    if(auto* Q=Find("Enabled"))L.Enabled=Q->On;
    if(auto* Q=Find("Cast Shadows"))L.CastShadows=Q->On;
    if(auto* Q=Find("Draw emitter"))L.DrawEmitter=Q->On;
    if(auto* Q=Find("Type"))L.Category=static_cast<PunctualLuminaireCategory>(std::min(Q->Picked,5u));
    if(auto* Q=Find("Distribution"))L.Distribution=static_cast<LuminaireDistribution>(std::min(Q->Picked,2u));
    if(auto* Q=Find("Intensity"))L.Intensity=std::max(0.f,Q->Figure);
    if(auto* Q=Find("Range"))L.Range=std::max(0.f,Q->Figure);
    if(auto* Q=Find("Colour"))CopyTint(L.Colour,Q->ColourTint);
    constexpr float DegToRad=.01745329251994329577f;if(auto* Q=Find("Inner Cone"))L.InnerConeAngle=std::clamp(Q->Figure*DegToRad,0.f,L.OuterConeAngle);if(auto* Q=Find("Outer Cone"))L.OuterConeAngle=std::clamp(Q->Figure*DegToRad,std::max(.0174533f,L.InnerConeAngle),1.5707964f);if(auto* Q=Find("Width"))L.Size[0]=std::max(.01f,Q->Figure);if(auto* Q=Find("Height"))L.Size[1]=std::max(.01f,Q->Figure);
    if(auto* Q=Find("Flux per metre")){L.LumensPerMetre=std::clamp(Q->Figure,10.f,4000.f);}
    if(auto* Q=Find("Load per metre")){L.WattsPerMetre=std::clamp(Q->Figure,1.f,50.f);}
    if(auto* Q=Find("Dimmer")){L.Dimmer=std::clamp(Q->Figure,0.f,1.f);}
    if(auto* Q=Find("Colour temperature")){L.Temperature=std::clamp(Q->Figure,1800.f,12000.f);}
    if(auto* Q=Find("Strip length")){L.Size[0]=std::clamp(Q->Figure,.1f,20.f);}
    if(auto* Q=Find("Emitter density")){L.EmittersPerMetre=std::clamp(Q->Figure,10.f,240.f);}
    if(auto* Q=Find("Supply voltage")){L.SupplyVoltage=std::clamp(Q->Figure,5.f,48.f);}
    if(auto* Q=Find("Opal diffuser")){L.Diffuser=Q->On;}
    if(L.Category==PunctualLuminaireCategory::Strip){L.Intensity=L.LumensPerMetre*L.Size[0]*L.Dimmer;}
    if(auto* Q=Find("Position"))for(int I=0;I<3;++I){P.WorldTransform[12+I]=Q->Axes[I];P.LocalTransform[12+I]=Q->Axes[I];}
}

bool EditorFeedSequence::QueryAnimatedSpan(uint32_t* First, uint32_t* Count,
                                          const SceneStructure& Level) const noexcept
{
    if (First == nullptr || Count == nullptr)
        return false;
    const auto& Placements = Level.QueryPlacements();
    const auto& Instances  = Level.QueryInstances();
    for (uint32_t P = 0u; P < Placements.size(); ++P)
    {
        const PlacementRecord& Head = Placements[P];
        if (!Head.Dynamic || Head.InstanceCount == 0u || Head.FirstInstance >= Instances.size())
            continue;
        uint32_t End = P;
        while (End + 1u < Placements.size())
        {
            const PlacementRecord& Next = Placements[End + 1u];
            if (!Next.Dynamic || Next.InstanceCount == 0u || Next.FirstInstance >= Instances.size())
                break;
            if (Next.FirstInstance != Placements[End].FirstInstance + Placements[End].InstanceCount)
                break;
            ++End;
        }
        *First = Head.FirstInstance;
        *Count = Placements[End].FirstInstance + Placements[End].InstanceCount - Head.FirstInstance;
        return true;
    }
    return false;
}

void EditorFeedSequence::ResolveRosterSpans(RosterSpan* Destination, const EditorInstance* Rows, uint32_t RowCount,
                                           const RosterSpan* Registered, uint32_t RegisteredCount) noexcept
{
    for (uint32_t Row = 0u; Row < RowCount; ++Row)
    {
        Destination[Row] = RosterSpan{};
        const uint64_t Key = Rows[Row].InspectorKey;
        const uint32_t Ordinal = static_cast<uint32_t>(Key);
        if ((Key >> 32u) == 1u && Ordinal > 0u && Ordinal <= RegisteredCount)
            Destination[Row] = Registered[Ordinal - 1u];
    }
}

std::vector<uint32_t> EditorFeedSequence::CollectSelectionInstances(const EditorInstance* Rows, uint32_t RowCount,
    const RosterSpan* Spans, const uint32_t* Picks, uint32_t PickCount, uint32_t InstanceCount)
{
    std::vector<uint32_t> Result;
    std::vector<bool> Included(InstanceCount, false);
    for (uint32_t Pick = 0u; Pick < PickCount; ++Pick)
    {
        const uint32_t First = Picks[Pick];
        if (First >= RowCount) continue;
        uint32_t Last = First + 1u;
        while (Last < RowCount && Rows[Last].Depth > Rows[First].Depth) ++Last;
        for (uint32_t Row = First; Row < Last; ++Row)
        {
            bool Locked = Rows[Row].Locked;
            uint32_t Depth = Rows[Row].Depth;
            for (uint32_t Ancestor = Row; Ancestor > 0u && Depth > 0u;)
            {
                --Ancestor;
                if (Rows[Ancestor].Depth < Depth) { Locked |= Rows[Ancestor].Locked; Depth = Rows[Ancestor].Depth; }
            }
            if (Locked) continue;
            const auto& Span = Spans[Row];
            if (Span.FirstInstance >= InstanceCount || Span.InstanceCount > InstanceCount - Span.FirstInstance) continue;
            for (uint32_t Offset = 0u; Offset < Span.InstanceCount; ++Offset)
            {
                const uint32_t Instance = Span.FirstInstance + Offset;
                if (!Included[Instance]) { Included[Instance] = true; Result.push_back(Instance); }
            }
        }
    }
    return Result;
}

uint32_t EditorFeedSequence::FillRosterSpans(RosterSpan* Spans, const SceneStructure& Level,
                                             uint32_t Capacity) const noexcept
{
    if (Spans == nullptr || Capacity == 0u)
        return 0u;
    const uint32_t TargetCapacity = std::min(Capacity, kMaxEditorInstances);
    std::vector<FeedRow> Layout(TargetCapacity);
    const uint32_t Rows = BuildLayout(Level, Layout.data(), TargetCapacity);
    const auto& Placements = Level.QueryPlacements();
    for (uint32_t R = 0u; R < Rows; ++R)
    {
        Spans[R] = RosterSpan{};
        if (Layout[R].Kind != FeedRowKind::Placement)
            continue;
        const PlacementRecord& P = Placements[Layout[R].Ordinal];
        Spans[R].Placement = Layout[R].Ordinal;
        if (P.InstanceCount > 0u && P.FirstInstance != kPlacementNone)
        {
            Spans[R].FirstInstance = P.FirstInstance;
            Spans[R].InstanceCount = P.InstanceCount;
        }
    }
    return Rows;
}

} // namespace Frontier::HostRuntime
