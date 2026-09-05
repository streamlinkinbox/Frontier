//============================================================================================================================================
//                                                  PHYSICSINSTANCESEQUENCE.CPP
//============================================================================================================================================

#include "PhysicsInstanceSequence.h"
#include "ShowroomStructure.h"

#include <algorithm>
#include <cmath>

namespace Frontier {
namespace ProjectZero {

namespace {

// Column-major, Columns[c][r] flattened to c*4 + r — the convention InstanceRecord::World uses.
void WriteIdentity(float* M) noexcept
{
    for (uint32_t I = 0u; I < 16u; ++I) M[I] = 0.0f;
    M[0] = M[5] = M[10] = M[15] = 1.0f;
}

// World = Translate(Position) · Rotate(Orientation) · Translate(−Rest).
//    Expanded directly rather than via three matrix products: the translation column becomes
//    Position − Rotate(Rest), which is the whole trick and is clearer written out than hidden in a multiply.
void WritePoseMatrix(float* M, const Quaternion& Q, const Vector3& Position, const Vector3& Rest) noexcept
{
    const float X = Q.x, Y = Q.y, Z = Q.z, W = Q.w;
    const float XX = X * X, YY = Y * Y, ZZ = Z * Z;
    const float XY = X * Y, XZ = X * Z, YZ = Y * Z;
    const float WX = W * X, WY = W * Y, WZ = W * Z;

    const float R00 = 1.0f - 2.0f * (YY + ZZ), R01 = 2.0f * (XY - WZ),        R02 = 2.0f * (XZ + WY);
    const float R10 = 2.0f * (XY + WZ),        R11 = 1.0f - 2.0f * (XX + ZZ), R12 = 2.0f * (YZ - WX);
    const float R20 = 2.0f * (XZ - WY),        R21 = 2.0f * (YZ + WX),        R22 = 1.0f - 2.0f * (XX + YY);

    M[0] = R00; M[1] = R10; M[2]  = R20; M[3]  = 0.0f;   // column 0
    M[4] = R01; M[5] = R11; M[6]  = R21; M[7]  = 0.0f;   // column 1
    M[8] = R02; M[9] = R12; M[10] = R22; M[11] = 0.0f;   // column 2

    M[12] = Position.x - (R00 * Rest.x + R01 * Rest.y + R02 * Rest.z);
    M[13] = Position.y - (R10 * Rest.x + R11 * Rest.y + R12 * Rest.z);
    M[14] = Position.z - (R20 * Rest.x + R21 * Rest.y + R22 * Rest.z);
    M[15] = 1.0f;
}

} // namespace

bool PhysicsInstanceSequence::Construct(RigidBodySolver& Solver, const PhysicsInstanceConfiguration& Configuration) noexcept
{
    Config = Configuration;
    Bodies.clear();
    RestOrigins.clear();
    Poses.clear();

    if (!Solver.IsReady()) return false;

    // ── Static colliders ─────────────────────────────────────────────────────────────────────────────────────────
    // Only the surfaces a falling ball can actually reach. The showroom's visual geometry is 16 806 triangles; a
    //    mesh collider over all of it would be wasteful and slow, and none of the detail changes where a sphere
    //    comes to rest. Boxes matching the floor, walls and plinth are exact for this scene.
    const auto AddStatic = [&](const char* Name, const Vector3& Centre, const Vector3& HalfExtent)
    {
        RigidBodyDescription D;
        D.Name             = Name;
        D.Motion           = RigidBodyMotionCategory::Static;
        D.Position         = Centre;
        D.Shape.Category   = CollisionShapeCategory::Box;
        D.Shape.HalfExtents = HalfExtent;
        D.Friction         = Config.Friction;
        D.Restitution      = Config.Restitution;
        (void)Solver.CreateBody(D);
    };

    // Room: 4.0 m wide (X ±2.0) × 5.0 deep (Y −2.0…+3.0) × 3.0 tall, open at −Y.
    AddStatic("floor",     Vector3{ 0.0f,  0.5f, -0.5f  }, Vector3{ 2.0f, 2.5f, 0.5f  });
    AddStatic("wall_left", Vector3{ -2.5f, 0.5f,  1.5f  }, Vector3{ 0.5f, 2.5f, 1.5f  });
    AddStatic("wall_right",Vector3{  2.5f, 0.5f,  1.5f  }, Vector3{ 0.5f, 2.5f, 1.5f  });
    AddStatic("wall_back", Vector3{  0.0f, 3.5f,  1.5f  }, Vector3{ 2.0f, 0.5f, 1.5f  });
    AddStatic("plinth",    Vector3{  0.0f, 1.55f, 0.175f}, Vector3{ 0.45f, 0.25f, 0.175f });

    // Invisible barrier across the room's open −Y face. The showroom is deliberately open there so the camera can
    //    see in, which means a rolling body would otherwise leave the world and fall forever. Containing it is a
    //    property of the simulation, not a fudge: nothing should be able to exit the volume the scene occupies.
    AddStatic("front_gate", Vector3{ 0.0f, -2.5f, 1.5f }, Vector3{ 2.0f, 0.5f, 1.5f });

    // ── Dynamic bodies, one per drop instance ────────────────────────────────────────────────────────────────────
    Bodies.reserve(Config.DropCount);
    RestOrigins.reserve(Config.DropCount);
    for (uint32_t Body = 0u; Body < Config.DropCount; ++Body)
    {
        const Vector3 Rest = ShowroomStructure::QueryDropOrigin(Body);

        RigidBodyDescription D;
        D.Name           = "drop_body";
        D.Motion         = RigidBodyMotionCategory::Dynamic;
        D.Position       = Rest;                       // physics starts exactly where the geometry was baked
        D.Shape.Category = CollisionShapeCategory::Sphere;
        D.Shape.Radius   = Config.BodyRadius;
        D.Friction       = Config.Friction;
        D.Restitution    = Config.Restitution;
        // A sphere on a frictionless-ish floor never stops rolling; damping stands in for rolling resistance so
        //    the scene actually settles and the proof can assert on it.
        D.AngularDamping = 0.85f;
        D.LinearDamping  = 0.10f;

        const RigidBodyIdentity Identity = Solver.CreateBody(D);
        if (Identity == InvalidRigidBody) continue;
        Bodies.push_back(Identity);
        RestOrigins.push_back(Rest);
    }

    Solver.OptimizeBroadPhase();   // once, after the static batch — never per frame
    Poses.reserve(Bodies.size() + 8u);
    return !Bodies.empty();
}

void PhysicsInstanceSequence::AdvancePhysics(RigidBodySolver& Solver, std::vector<InstanceRecord>& Rows, float DeltaSeconds) noexcept
{
    Solver.Advance(DeltaSeconds);

    ActiveCount  = 0u;
    LowestHeight = 1e30f;

    for (uint32_t Body = 0u; Body < Bodies.size(); ++Body)
    {
        const uint32_t Slot = Config.FirstDropInstance + Body;
        if (Slot >= Rows.size()) break;

        RigidBodyPose Pose;
        if (!Solver.QueryPose(Bodies[Body], Pose)) continue;

        InstanceRecord& Row = Rows[Slot];
        // PreviousWorld must carry LAST frame's World or motion vectors and ReSTIR reprojection smear.
        for (uint32_t E = 0u; E < 16u; ++E) Row.PreviousWorld[E] = Row.World[E];
        WritePoseMatrix(Row.World, Pose.Orientation, Pose.Position, RestOrigins[Body]);

        if (Pose.Active) ++ActiveCount;
        LowestHeight = std::min(LowestHeight, Pose.Position.z);
    }

    if (LowestHeight > 1e29f) LowestHeight = 0.0f;
}

} // namespace ProjectZero
} // namespace Frontier
