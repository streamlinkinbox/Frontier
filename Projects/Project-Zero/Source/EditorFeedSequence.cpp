//============================================================================================================================================
//                                                  EDITORFEEDSEQUENCE.CPP
//============================================================================================================================================
// 🧩 The development editor's live feed — FillRoster walks the level's placements into outliner rows, BuildSheet
//    reads the picked row's figures live, and QueryAnimatedSpan finds the run the motion driver owns. Stateless
//    over the level: both builders recompute the same row layout from it, so the sheet can never disagree
//    with the roster.

#include "EditorFeedSequence.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::ProjectZero {
namespace {

// The roster's rows, in order. Folders are virtual (Ordinal = folder index); placement rows carry their
//    placement ordinal; the fly camera is stock (Ordinal unused).
enum class FeedRowKind : uint32_t { Folder, Placement, FlyCamera };
struct FeedRow
{
    FeedRowKind Kind    = FeedRowKind::Folder;
    uint32_t    Ordinal = 0u;
};

constexpr uint32_t kFolderRoom        = 0u;
constexpr uint32_t kFolderObjects     = 1u;
constexpr uint32_t kFolderLighting    = 2u;
constexpr uint32_t kFolderCameras     = 3u;
constexpr const char* kFolderLabels[] = { "Room", "Objects", "Lighting", "Cameras" };

constexpr float kFolderTint[3] = { 0.788f, 0.635f, 0.294f };   // amber, shared by every folder
constexpr float kLightTint[3]  = { 0.961f, 0.827f, 0.294f };   // the lamp rows
constexpr float kCameraTint[3] = { 0.412f, 0.765f, 1.000f };   // the camera rows

// True when any of the placement's instances sits on an emissive material. Reads the flattened records —
//    emission_luminance × emission_color — so the test matches what the kernel lights from.
bool PlacementEmits(const PlacementRecord& P, const SceneStructure& Level) noexcept
{
    const auto& Instances = Level.QueryInstances();
    const auto& Records   = Level.QueryMaterials().QueryRecords();
    if (P.FirstInstance >= Instances.size()
        || P.InstanceCount > Instances.size() - P.FirstInstance)
        return false;
    for (uint32_t I = 0u; I < P.InstanceCount; ++I)
    {
        const uint32_t Slot = Instances[P.FirstInstance + I].MaterialIndex;
        if (Slot < Records.size())
        {
            const MaterialRecord& R = Records[Slot];
            if (R.EmissiveR + R.EmissiveG + R.EmissiveB > 0.0f)
                return true;
        }
    }
    return false;
}

// Each placement belongs to exactly one folder: file cameras under Cameras, emissive and luminaire carriers
//    under Lighting, the flagged dynamics under Objects, and everything else — the static scenery — under Room.
//    Vector order within a folder, so Room reads in build order and Objects in body order.
uint32_t PlacementFolder(const PlacementRecord& P, const SceneStructure& Level) noexcept
{
    if (P.Camera != kPlacementNone)
        return kFolderCameras;
    if (PlacementEmits(P, Level)
        || (P.Luminaire != kPlacementNone && P.Luminaire < Level.QueryPunctualLuminaires().size()))
        return kFolderLighting;
    return P.Dynamic ? kFolderObjects : kFolderRoom;
}

// Depth below its folder: roots sit at 1, nested placements deepen. The ancestor chase is capped so a corrupt
//    link idles at the root instead of looping.
uint32_t PlacementDepth(uint32_t Ordinal, const SceneStructure& Level) noexcept
{
    const auto& Placements = Level.QueryPlacements();
    uint32_t Depth = 1u;
    uint32_t Walk  = Ordinal;
    for (uint32_t Hops = 0u; Hops < 8u && Walk < Placements.size(); ++Hops)
    {
        const uint32_t Parent = Placements[Walk].Ancestor;
        if (Parent == kPlacementNone || Parent >= Placements.size())
            break;
        ++Depth;
        Walk = Parent;
    }
    return Depth;
}

// The shared row layout: each folder followed by its placements, the fly camera first under Cameras.
//    Both builders run this, so the sheet's row means what the roster showed.
uint32_t BuildLayout(const SceneStructure& Level, FeedRow* Layout, uint32_t Capacity) noexcept
{
    const auto& Placements = Level.QueryPlacements();
    uint32_t Rows = 0u;
    auto Push = [&](FeedRowKind Kind, uint32_t Ordinal)
    {
        if (Rows < Capacity) { Layout[Rows].Kind = Kind; Layout[Rows].Ordinal = Ordinal; ++Rows; }
    };
    for (uint32_t Folder = 0u; Folder < kFolderCameras; ++Folder)
    {
        Push(FeedRowKind::Folder, Folder);
        for (uint32_t P = 0u; P < Placements.size(); ++P)
            if (PlacementFolder(Placements[P], Level) == Folder)
                Push(FeedRowKind::Placement, P);
    }
    Push(FeedRowKind::Folder, kFolderCameras);
    Push(FeedRowKind::FlyCamera, 0u);
    for (uint32_t P = 0u; P < Placements.size(); ++P)
        if (PlacementFolder(Placements[P], Level) == kFolderCameras)
            Push(FeedRowKind::Placement, P);
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

// The emission direction snapped to its dominant world axis — "-Z (nadir)" for the Cornell luminaire.
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

uint32_t EditorFeedSequence::FillRoster(EditorInstance* Instances, const SceneStructure& Level) const noexcept
{
    if (Instances == nullptr)
        return 0u;
    FeedRow Layout[kMaxEditorInstances];
    const uint32_t Rows = BuildLayout(Level, Layout, kMaxEditorInstances);
    const auto& Placements = Level.QueryPlacements();
    const auto& LevelInstances = Level.QueryInstances();
    const auto& Records = Level.QueryMaterials().QueryRecords();

    for (uint32_t R = 0u; R < Rows; ++R)
    {
        EditorInstance& Row = Instances[R];
        Row.Visible = true;
        Row.Locked  = false;
        Row.Solo    = false;
        Row.Physics = false;
        const FeedRow& Entry = Layout[R];
        switch (Entry.Kind)
        {
        case FeedRowKind::Folder:
            std::snprintf(Row.Label, sizeof(Row.Label), "%s", kFolderLabels[Entry.Ordinal]);
            Row.Depth    = 0u;
            Row.Category = EditorInstanceCategory::Folder;
            CopyTint(Row.Tint, kFolderTint);
            break;
        case FeedRowKind::Placement:
        {
            const PlacementRecord& P = Placements[Entry.Ordinal];
            if (!P.Name.empty()) std::snprintf(Row.Label, sizeof(Row.Label), "%s", P.Name.c_str());
            else                 std::snprintf(Row.Label, sizeof(Row.Label), "Object %u", Entry.Ordinal);
            Row.Depth = PlacementDepth(Entry.Ordinal, Level);
            Row.Dynamic = P.Dynamic;
            const uint32_t Folder = PlacementFolder(P, Level);
            if (Folder == kFolderLighting)
            {
                Row.Category = EditorInstanceCategory::Light;
                CopyTint(Row.Tint, kLightTint);
            }
            else if (Folder == kFolderCameras)
            {
                Row.Category = EditorInstanceCategory::Camera;
                CopyTint(Row.Tint, kCameraTint);
            }
            else
            {
                Row.Category = EditorInstanceCategory::Geometry;
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
            std::snprintf(Row.Label, sizeof(Row.Label), "Main Camera");
            Row.Depth    = 1u;
            Row.Category = EditorInstanceCategory::Camera;
            CopyTint(Row.Tint, kCameraTint);
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
                if (Vx[K] < Lo[0]) Lo[0] = Vx[K]; if (Vx[K] > Hi[0]) Hi[0] = Vx[K];
                if (Vy[K] < Lo[1]) Lo[1] = Vy[K]; if (Vy[K] > Hi[1]) Hi[1] = Vy[K];
                if (Vz[K] < Lo[2]) Lo[2] = Vz[K]; if (Vz[K] > Hi[2]) Hi[2] = Vz[K];
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
    Sheet->GroupCount = 0u;
    if (Instances == nullptr || Index >= RowCount || RowCount > kMaxEditorInstances)
        return nullptr;

    using Frontier::EditorPropertyCategory;
    constexpr float kRadToDeg = 57.29578f;
    FeedRow Layout[kMaxEditorInstances];
    const uint32_t StockRows = BuildLayout(Level, Layout, RowCount);
    if (Index >= StockRows)
        return nullptr;
    const FeedRow& Picked = Layout[Index];
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
            EditorPropertyGroup& Lamp = OpenGroup(Sheet, "Light");
            EditorProperty& Power = OpenProp(Lamp, "Intensity", EditorPropertyCategory::Slider);
            EditorProperty& Hue = OpenProp(Lamp, "Colour", EditorPropertyCategory::Colour);
            const auto& Punctuals = Level.QueryPunctualLuminaires();
            const bool HasPunctual = P.Luminaire != kPlacementNone && P.Luminaire < Punctuals.size();
            if (Mat != nullptr && (Mat->EmissiveR + Mat->EmissiveG + Mat->EmissiveB) > 0.0f)
            {
                const float Brightest = Mat->EmissiveR > Mat->EmissiveG
                    ? (Mat->EmissiveR > Mat->EmissiveB ? Mat->EmissiveR : Mat->EmissiveB)
                    : (Mat->EmissiveG > Mat->EmissiveB ? Mat->EmissiveG : Mat->EmissiveB);
                Power.Minimum = 0.0f; Power.Maximum = 64.0f; Power.Figure = Mat->EmissiveR;
                Power.Decimals = 1u;
                std::snprintf(Power.Unit, sizeof(Power.Unit), "lx");
                Hue.ColourTint[0] = Brightest > 0.0f ? Mat->EmissiveR / Brightest : 1.0f;
                Hue.ColourTint[1] = Brightest > 0.0f ? Mat->EmissiveG / Brightest : 1.0f;
                Hue.ColourTint[2] = Brightest > 0.0f ? Mat->EmissiveB / Brightest : 1.0f;
            }
            else if (HasPunctual)
            {
                const PunctualLuminaireRecord& L = Punctuals[P.Luminaire];
                Power.Minimum = 0.0f; Power.Maximum = 64.0f; Power.Figure = L.Intensity;
                Power.Decimals = 1u;
                std::snprintf(Power.Unit, sizeof(Power.Unit),
                              L.Category == PunctualLuminaireCategory::Directional ? "lx" : "cd");
                CopyTint(Hue.ColourTint, L.Colour);
            }
            EditorPropertyGroup& Aimed = OpenGroup(Sheet, "Aim");
            EditorProperty& Facing = OpenProp(Aimed, "Direction", EditorPropertyCategory::Readout);
            if (Mat != nullptr && (Mat->EmissiveR + Mat->EmissiveG + Mat->EmissiveB) > 0.0f)
                FormatDirection(Facing.Text, Normal);
            else if (HasPunctual)
            {
                // A file light shines down its node's −Z: negate the world Z axis and snap it.
                const float Down[3] = { -P.WorldTransform[8], -P.WorldTransform[9], -P.WorldTransform[10] };
                FormatDirection(Facing.Text, Down);
            }
            else
                std::snprintf(Facing.Text, sizeof(Facing.Text), "—");
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

    if (Picked.Kind == FeedRowKind::FlyCamera)
    {
        const Frontier::Vector3 At     = Camera.Convert<Frontier::Vector3>();
        const auto&             Flight = Camera.QueryConfiguration();
        EditorPropertyGroup& Placed = OpenGroup(Sheet, "Transform");
        EditorProperty& Where = OpenProp(Placed, "Position", EditorPropertyCategory::AxisVec3);
        Where.Axes[0] = At.x;
        Where.Axes[1] = At.y;
        Where.Axes[2] = At.z;
        Where.AxisStep = 0.05f;
        Where.Editable = false;
        EditorProperty& Pitch = OpenProp(Placed, "Pitch", EditorPropertyCategory::Readout);
        std::snprintf(Pitch.Text, sizeof(Pitch.Text), "%+.1f\xc2\xb0",
            static_cast<double>(Camera.QueryPitchRadians() * kRadToDeg));
        EditorProperty& Yaw = OpenProp(Placed, "Yaw", EditorPropertyCategory::Readout);
        std::snprintf(Yaw.Text, sizeof(Yaw.Text), "%+.1f\xc2\xb0",
            static_cast<double>(Camera.QueryYawRadians() * kRadToDeg));
        EditorPropertyGroup& Lens = OpenGroup(Sheet, "Lens");
        EditorProperty& Wide = OpenProp(Lens, "Field of view", EditorPropertyCategory::Slider);
        Wide.Minimum = 20.0f; Wide.Maximum = 120.0f;
        Wide.Figure = Camera.QueryFieldOfViewRadians() * kRadToDeg;
        Wide.Decimals = 1u; Wide.Hi = true;
        std::snprintf(Wide.Unit, sizeof(Wide.Unit), "\xc2\xb0");
        EditorProperty& Shape = OpenProp(Lens, "Aspect", EditorPropertyCategory::Readout);
        std::snprintf(Shape.Text, sizeof(Shape.Text), "%.3f", static_cast<double>(Camera.QueryAspectRatio()));
        EditorPropertyGroup& Moved = OpenGroup(Sheet, "Flight");
        EditorProperty& Fast = OpenProp(Moved, "Speed", EditorPropertyCategory::Readout);
        std::snprintf(Fast.Text, sizeof(Fast.Text), "%.2f m/s", static_cast<double>(Camera.QueryFlightSpeed()));
        EditorProperty& BaseProp = OpenProp(Moved, "Cruise", EditorPropertyCategory::Readout);
        std::snprintf(BaseProp.Text, sizeof(BaseProp.Text), "%.2f m/s", static_cast<double>(Flight.BaseFlightSpeed));
        EditorProperty& Boost = OpenProp(Moved, "Boost", EditorPropertyCategory::Readout);
        std::snprintf(Boost.Text, sizeof(Boost.Text), "%.2f\xc3\x97", static_cast<double>(Flight.BoostMultiplier));
        EditorProperty& Feel = OpenProp(Moved, "Sensitivity", EditorPropertyCategory::Readout);
        std::snprintf(Feel.Text, sizeof(Feel.Text), "%.5f rad/px", static_cast<double>(Flight.MouseSensitivity));
        return nullptr;
    }

    return nullptr;
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

} // namespace Frontier::ProjectZero
