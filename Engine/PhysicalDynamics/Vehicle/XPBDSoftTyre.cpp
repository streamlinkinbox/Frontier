//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/XPBDSoftTyre.cpp
//============================================================================================================================================

#include "XPBDSoftTyre.h"

#include <algorithm>
#include <utility>

namespace Frontier::Vehicle {

namespace { constexpr float kPi = 3.14159265358979323846f; }

//------------------------------------------------------------------------------------------------------------------------
//                                                      BUILD
//------------------------------------------------------------------------------------------------------------------------
void XPBDSoftTyre::Build(const SoftTyreParameters& params, const Vec3& hubPos, const Quat& hubRot) noexcept
{
    Parameters = params;
    Parameters.RingCount    = std::max<uint32_t>(2u, Parameters.RingCount);
    Parameters.SegmentCount = std::max<uint32_t>(8u, Parameters.SegmentCount);

    const uint32_t R = Parameters.RingCount, S = Parameters.SegmentCount;
    const uint32_t N = R * S;
    NodeRecords.assign(N, SoftTyreNode{});
    SpinAngle = 0.0f;

    const float perNodeMass = (Parameters.TotalMass > 0.0f) ? Parameters.TotalMass / static_cast<float>(N) : 0.0f;
    const float invMass     = (perNodeMass > 0.0f) ? 1.0f / perNodeMass : 0.0f;

    // Spin axis = +Y (hub-local); ring plane = X/Z. Rings distributed across the width in Y.
    for (uint32_t r = 0u; r < R; ++r)
    {
        const float yOff = (R > 1u)
            ? (-0.5f * Parameters.Width + Parameters.Width * (static_cast<float>(r) / static_cast<float>(R - 1u)))
            : 0.0f;
        for (uint32_t s = 0u; s < S; ++s)
        {
            const float ang = (2.0f * kPi * static_cast<float>(s)) / static_cast<float>(S);
            const float c = std::cos(ang), sn = std::sin(ang);
            SoftTyreNode& node = NodeRecords[Index(r, s)];
            node.TreadLocal = {Parameters.Radius * c, yOff, Parameters.Radius * sn};
            node.BeadLocal  = {Parameters.RimRadius * c, yOff, Parameters.RimRadius * sn};
            node.Position   = hubPos + hubRot.Rotate(node.TreadLocal);
            node.Previous   = node.Position;
            node.Velocity   = {0, 0, 0};
            node.InverseMass = invMass;
        }
    }

    SpokeBeta   = DerivedDamping(Parameters.SpokeCompliance, Parameters.SpokeDampingRatio);
    SpokeTangentialBeta = DerivedDamping(Parameters.SpokeTangentialCompliance, Parameters.SpokeShearDampingRatio);
    SpokeLateralBeta    = DerivedDamping(Parameters.SpokeLateralCompliance, Parameters.SpokeShearDampingRatio);
    ContactBeta = DerivedDamping(Parameters.ContactCompliance, Parameters.ContactDampingRatio);
    TreadBeta   = DerivedDamping(Parameters.TreadTangentialCompliance, Parameters.TreadDampingRatio);
    RimBeta     = DerivedDamping(Parameters.RimBottomingCompliance, Parameters.RimBottomingDampingRatio);

    BuildEdges();
    BuildGasSurface();
    // Orientation is decided by measurement, not by trusting four hand-wound patches: if the surface came out
    // inward-facing the signed volume is negative, so every triangle is reversed once and the sign checked.
    GasRestVolume = MeasureInflation(hubPos, hubRot);
    if (GasRestVolume < 0.0f)
    {
        for (SurfaceTriangle& tri : GasSurface) std::swap(tri.b, tri.c);
        GasRestVolume = MeasureInflation(hubPos, hubRot);
    }
    ContactReaction = TyreReaction{};
}

float XPBDSoftTyre::DerivedDamping(float compliance, float ratio) const noexcept
{
    if (!(compliance > 0.0f) || !(ratio > 0.0f) || NodeRecords.empty()) return 0.0f;
    const float perNodeMass = (Parameters.TotalMass > 0.0f)
                            ? Parameters.TotalMass / static_cast<float>(NodeRecords.size()) : 0.0f;
    if (!(perNodeMass > 0.0f)) return 0.0f;
    return 2.0f * ratio * std::sqrt(perNodeMass / compliance);   // 2ζ√(k·m), k = 1/α
}

void XPBDSoftTyre::BuildEdges() noexcept
{
    ConstraintEdges.clear();
    const uint32_t R = Parameters.RingCount, S = Parameters.SegmentCount;

    auto restLen = [&](uint32_t a, uint32_t b) { return (NodeRecords[a].TreadLocal - NodeRecords[b].TreadLocal).Length(); };

    for (uint32_t r = 0u; r < R; ++r)
        for (uint32_t s = 0u; s < S; ++s)
        {
            const uint32_t i = Index(r, s);
            const uint32_t sn = (s + 1u) % S;

            // Hoop (circumferential, same ring) — always present.
            {
                const uint32_t j = Index(r, sn);
                ConstraintEdges.push_back({i, j, restLen(i, j), Parameters.HoopCompliance, DerivedDamping(Parameters.HoopCompliance, Parameters.HoopDampingRatio), EdgeFamily::Hoop});
            }
            if (r + 1u < R)
            {
                // Lateral (same segment, next ring).
                const uint32_t j = Index(r + 1u, s);
                ConstraintEdges.push_back({i, j, restLen(i, j), Parameters.LateralCompliance, DerivedDamping(Parameters.LateralCompliance, Parameters.LateralDampingRatio), EdgeFamily::Lateral});
                // Shear diagonal (next segment, next ring).
                const uint32_t k = Index(r + 1u, sn);
                ConstraintEdges.push_back({i, k, restLen(i, k), Parameters.ShearCompliance, DerivedDamping(Parameters.ShearCompliance, Parameters.ShearDampingRatio), EdgeFamily::Diagonal});
                // Anti-diagonal (this ring's next seg ↔ next ring's this seg) for symmetric shear.
                const uint32_t a = Index(r, sn), b = Index(r + 1u, s);
                ConstraintEdges.push_back({a, b, restLen(a, b), Parameters.ShearCompliance, DerivedDamping(Parameters.ShearCompliance, Parameters.ShearDampingRatio), EdgeFamily::Diagonal});
            }
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                       THE CLOSED SURFACE THE GAS PUSHES ON
//------------------------------------------------------------------------------------------------------------------------
//    Tread band, both sidewalls down to their bead circles, and the rim barrel that caps the two beads. The
//    barrel is entirely kinematic — it contributes volume and takes no force — but it has to be there, or the
//    surface is a tube, the volume is undefined, and the load path through the rim does not exist.
void XPBDSoftTyre::BuildGasSurface() noexcept
{
    const uint32_t R = Parameters.RingCount, S = Parameters.SegmentCount;
    const uint32_t N = R * S;

    BeadLocalVertices.assign(2u * S, Vec3{});
    for (uint32_t s = 0u; s < S; ++s)
    {
        BeadLocalVertices[s]     = NodeRecords[Index(0u, s)].BeadLocal;
        BeadLocalVertices[S + s] = NodeRecords[Index(R - 1u, s)].BeadLocal;
    }
    BeadWorldVertices.assign(BeadLocalVertices.size(), Vec3{});
    InflationGradient.assign(N, Vec3{});

    GasSurface.clear();
    GasSurface.reserve(static_cast<size_t>(S) * (2u * (R - 1u) + 6u));
    for (uint32_t s = 0u; s < S; ++s)
    {
        const uint32_t sn = (s + 1u) % S;
        for (uint32_t r = 0u; r + 1u < R; ++r)                                   // tread band
        {
            const uint32_t A = Index(r, s), B = Index(r, sn), C = Index(r + 1u, sn), D = Index(r + 1u, s);
            GasSurface.push_back({A, B, C});
            GasSurface.push_back({A, C, D});
        }
        const uint32_t b0 = N + s,         b0n = N + sn;                          // bead circle, −Y side
        const uint32_t b1 = N + S + s,     b1n = N + S + sn;                      // bead circle, +Y side
        const uint32_t i0 = Index(0u, s),  i0n = Index(0u, sn);
        const uint32_t i1 = Index(R - 1u, s), i1n = Index(R - 1u, sn);
        GasSurface.push_back({b0, b0n, i0n});                                     // sidewall, −Y side
        GasSurface.push_back({b0, i0n, i0});
        GasSurface.push_back({i1, i1n, b1n});                                     // sidewall, +Y side
        GasSurface.push_back({i1, b1n, b1});
        GasSurface.push_back({b0, b1, b1n});                                      // rim barrel (kinematic)
        GasSurface.push_back({b0, b1n, b0n});
    }
}

//    V = ⅙ Σ x₀·(x₁ × x₂) with ∂V/∂x₀ = ⅙ (x₁ × x₂), evaluated HUB-RELATIVE. Both are translation-invariant
//    for a closed surface (the one-ring cross terms telescope away), and working at the origin keeps a tyre a
//    kilometre down the course from losing the volume in the cancellation.
float XPBDSoftTyre::MeasureInflation(const Vec3& hubPos, const Quat& frame) noexcept
{
    const uint32_t N = static_cast<uint32_t>(NodeRecords.size());
    for (size_t v = 0u; v < BeadLocalVertices.size(); ++v)
        BeadWorldVertices[v] = frame.Rotate(BeadLocalVertices[v]);

    for (Vec3& g : InflationGradient) g = Vec3{};

    const float sixth = 1.0f / 6.0f;
    double volume = 0.0;
    for (const SurfaceTriangle& tri : GasSurface)
    {
        const Vec3 a = (tri.a < N) ? NodeRecords[tri.a].Position - hubPos : BeadWorldVertices[tri.a - N];
        const Vec3 b = (tri.b < N) ? NodeRecords[tri.b].Position - hubPos : BeadWorldVertices[tri.b - N];
        const Vec3 c = (tri.c < N) ? NodeRecords[tri.c].Position - hubPos : BeadWorldVertices[tri.c - N];

        const Vec3 bc = Cross(b, c);
        volume += static_cast<double>(Dot(a, bc));
        if (tri.a < N) InflationGradient[tri.a] += bc * sixth;
        if (tri.b < N) InflationGradient[tri.b] += Cross(c, a) * sixth;
        if (tri.c < N) InflationGradient[tri.c] += Cross(a, b) * sixth;
    }
    return static_cast<float>(volume / 6.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      STEP
//------------------------------------------------------------------------------------------------------------------------
void XPBDSoftTyre::Step(float dt, uint32_t substeps, const HubMotion& motion,
                        const Vec3& surfaceVelocity, const GroundQuery& ground) noexcept
{
    ContactReaction = TyreReaction{};
    if (NodeRecords.empty() || !(dt > 0.0f)) return;

    substeps = std::max<uint32_t>(1u, substeps);
    const float h = dt / static_cast<float>(substeps);
    const float h2 = h * h;
    const float invH2 = 1.0f / h2;

    const uint32_t iterations = std::max<uint32_t>(1u, Parameters.SolverIterations);
    float RimArrivalAccum = 0.0f;   // [m] deepest flange interference handed to the projection this step
    const Quat hubRot    = motion.Orientation;
    const Vec3 axisWorld = hubRot.Rotate({0, 1, 0}).Normalized();   // spin axis in world

    Vec3  sumForce{};
    Vec3  sumTorqueRef{};    // torque of contact forces about hub ground projection (for Mz)
    Vec3  sumPatch{};
    float patchN = 0.0f;
    float contactAccum = 0.0f;

    const float alphaContact = Parameters.ContactCompliance * invH2;
    const float gammaContact = Parameters.ContactCompliance * ContactBeta / h;

    Quat  frame      = MaterialFrame(hubRot);
    Vec3  hubPos     = motion.Position;
    float gasVolume  = GasRestVolume;
    float gasGauge   = Parameters.InflationPressure;

    for (uint32_t sub = 0u; sub < substeps; ++sub)
    {
        // ── walk the kinematic hub into this substep ─────────────────────────────────────────────────────────────
        // `motion` describes the END of the step, so the pose at the end of substep k is the end pose rewound by
        // the travel still to come. The carcass's own rotation accumulates at the same rate, which is what makes
        // the contact patch a standing deformation in the world while the material rolls through it.
        const Vec3 hubPosPrev = motion.Position - motion.LinearVelocity * (static_cast<float>(substeps - sub) * h);
        const Quat framePrev  = MaterialFrame(hubRot);
        hubPos = motion.Position - motion.LinearVelocity * (static_cast<float>(substeps - 1u - sub) * h);
        SpinAngle += motion.SpinRate * h;
        if (SpinAngle > kPi || SpinAngle < -kPi) SpinAngle = std::fmod(SpinAngle + kPi, 2.0f * kPi) - kPi;
        frame = MaterialFrame(hubRot);

        // ── reset the Lagrange multipliers ───────────────────────────────────────────────────────────────────────
        // XPBD §3.2: λ is zeroed at the START of each substep and accumulated across that substep's iterations.
        // Carrying it between substeps would make the constraint remember a force it has already applied.
        for (SoftTyreNode& node : NodeRecords)
        { node.SpokeLambda = 0.0f; node.ContactLambda = 0.0f; node.TreadLambda = 0.0f; node.RimLambda = 0.0f;
          node.SpokeTangentialLambda = 0.0f; node.SpokeLateralLambda = 0.0f; }
        for (Edge& e : ConstraintEdges) e.lambda = 0.0f;

        // ── predict: gravity + the gas ───────────────────────────────────────────────────────────────────────────
        // The inflation force is p ∂V/∂x, measured on the deformed surface, so a flattened patch really does
        // leave the rim with p·A_patch of unbalanced lift instead of the ~0 N a radial P·A field produced.
        gasVolume = MeasureInflation(hubPos, frame);
        gasGauge  = Parameters.InflationPressure;
        if (Parameters.GasCompressible && gasVolume > 1e-6f && GasRestVolume > 1e-6f)
        {
            const float absolute = (Parameters.InflationPressure + Parameters.AtmosphericPressure)
                                 * (GasRestVolume / gasVolume);                  // Boyle, isothermal
            gasGauge = absolute - Parameters.AtmosphericPressure;
        }
        gasGauge = std::max(0.0f, gasGauge);   // gas pushes; a tyre never sucks its tread inward

        for (uint32_t i = 0u; i < static_cast<uint32_t>(NodeRecords.size()); ++i)
        {
            SoftTyreNode& node = NodeRecords[i];
            if (node.InverseMass <= 0.0f) { node.Previous = node.Position; continue; }

            const Vec3 pressureForce = InflationGradient[i] * gasGauge;
            const Vec3 accel = Parameters.Gravity + pressureForce * node.InverseMass;

            node.Velocity += accel * h;
            node.Previous = node.Position;
            node.Position += node.Velocity * h;
        }

        // ── project: Gauss-Seidel sweeps over every constraint ───────────────────────────────────────────────────
        // Macklin et al. 2019 prefer substeps to iterations, and one sweep per substep is the small-steps
        // default. It is not always enough HERE, and the measurement says so: the quantity the vehicle
        // consumes is the ground reaction, and a sweep that has not converged under-reports it. On a 40 mm
        // settle the reaction read 8.3 kN at 4 substeps against 12.6 kN for the lattice's own elastic state,
        // and only crept to 11.7 kN at 64. The sweep count is therefore a parameter with a measured gate
        // behind it rather than an assumption baked into the loop.
        for (uint32_t iteration = 0u; iteration < iterations; ++iteration)
        {
            // ── project: spokes (node ↔ rim/bead anchor) ─────────────────────────────────────────────────────────────
            // XPBD eq. 18 throughout: Δλ = (−C − α̃λ)/(Σw|∇C|² + α̃), Δx = w ∇C Δλ, λ += Δλ.  |∇C| = 1 for a
            // distance constraint, so the denominator is Σw + α̃.
            const float alphaSpoke = Parameters.SpokeCompliance * invH2;
            const float gammaSpoke = Parameters.SpokeCompliance * SpokeBeta / h;   // γ = α·β/Δt
            const float alphaSpokeTangential = Parameters.SpokeTangentialCompliance * invH2;
            const float alphaSpokeLateral    = Parameters.SpokeLateralCompliance * invH2;
            const float gammaSpokeTangential = Parameters.SpokeTangentialCompliance * SpokeTangentialBeta / h;
            const float gammaSpokeLateral    = Parameters.SpokeLateralCompliance * SpokeLateralBeta / h;
            const float spokeRest = Parameters.Radius - Parameters.RimRadius;
            for (SoftTyreNode& node : NodeRecords)
            {
                if (node.InverseMass <= 0.0f) continue;
                const Vec3 anchor     = hubPos     + frame.Rotate(node.BeadLocal);
                const Vec3 anchorPrev = hubPosPrev + framePrev.Rotate(node.BeadLocal);
                Vec3 d = node.Position - anchor;
                const float dist = d.Length();
                if (dist < 1e-6f) continue;
                const Vec3 dir = d * (1.0f / dist);
                const float C = dist - spokeRest;
                // Tension-only: a cord sidewall shorter than its rest length has buckled and carries nothing.
                // Dropping the projection (and the multiplier with it) is the unilateral form of eq. 18.
                const bool slack = Parameters.SidewallTensionOnly && C < 0.0f;
                if (slack)
                {
                    node.SpokeLambda = 0.0f;
                }
                else
                {
                    // eq. 26: the damping term is the rate the CONSTRAINT is being violated, Ċ = ∇C·(ẋ − ẋ_anchor).
                    // Subtracting the anchor's own motion is not a refinement, it is the difference between a
                    // damper and a brake: against a hub that now travels and spins, ∇C·(x − xⁿ) alone measures
                    // the carcass's absolute motion, and at 36 m/s that is a tangential 18.8 mm per substep
                    // through a 55 N·s/m dashpot — kilonewtons of drag on a tyre that is merely rolling.
                    const float cDot = Dot(dir, (node.Position - node.Previous) - (anchor - anchorPrev));
                    const float dLambda = (-C - alphaSpoke * node.SpokeLambda - gammaSpoke * cDot)
                                        / ((1.0f + gammaSpoke) * node.InverseMass + alphaSpoke);
                    node.SpokeLambda += dLambda;
                    node.Position += dir * (dLambda * node.InverseMass);
                }

                // ── shear spokes: hold the node on its own radial LINE, not just its distance sphere ─────────────
                // Radially soft, tangentially and axially stiff. Without these the band drifts off the hub (14.6 mm
                // measured) and every node's radius about the hub reads scattered even when the band is round.
                const Vec3 RestWorld = hubPos     + frame.Rotate(node.TreadLocal);
                const Vec3 RestPrev  = hubPosPrev + framePrev.Rotate(node.TreadLocal);
                const Vec3 Offset = node.Position - RestWorld;
                const Vec3 Relative = (node.Position - node.Previous) - (RestWorld - RestPrev);
                const Vec3 AxisDir = axisWorld;
                Vec3 RadialDir = RestWorld - hubPos;
                RadialDir = RadialDir - AxisDir * Dot(RadialDir, AxisDir);
                const float RadialLen = RadialDir.Length();
                if (RadialLen > 1e-6f)
                {
                    RadialDir = RadialDir * (1.0f / RadialLen);
                    const Vec3 TangentDir = Cross(AxisDir, RadialDir);   // unit: both are unit and orthogonal
                    const Vec3 ShearDir[2] = { TangentDir, AxisDir };
                    const float ShearAlpha[2] = { alphaSpokeTangential, alphaSpokeLateral };
                    const float ShearGamma[2] = { gammaSpokeTangential, gammaSpokeLateral };
                    float* ShearLambda[2] = { &node.SpokeTangentialLambda, &node.SpokeLateralLambda };
                    for (int K = 0; K < 2; ++K)
                    {
                        const float Cs = Dot(Offset, ShearDir[K]);
                        const float CsDot = Dot(ShearDir[K], Relative);
                        const float dL = (-Cs - ShearAlpha[K] * (*ShearLambda[K]) - ShearGamma[K] * CsDot)
                                       / ((1.0f + ShearGamma[K]) * node.InverseMass + ShearAlpha[K]);
                        *ShearLambda[K] += dL;
                        node.Position += ShearDir[K] * (dL * node.InverseMass);
                    }
                }
            }

            // ── project: internal lattice edges (hoop / lateral / shear) ─────────────────────────────────────────────
            for (Edge& e : ConstraintEdges)
            {
                SoftTyreNode& A = NodeRecords[e.a];
                SoftTyreNode& B = NodeRecords[e.b];
                const float wsum = A.InverseMass + B.InverseMass;
                if (wsum <= 0.0f) continue;
                Vec3 d = B.Position - A.Position;
                const float dist = d.Length();
                if (dist < 1e-6f) continue;
                const Vec3 dir = d * (1.0f / dist);
                const float C = dist - e.rest;
                const float alpha = e.compliance * invH2;
                const float gamma = e.compliance * e.damping / h;                      // γ = α·β/Δt (eq. 26)
                const float cDot = Dot(dir, (B.Position - B.Previous) - (A.Position - A.Previous));
                const float dLambda = (-C - alpha * e.lambda - gamma * cDot)
                                    / ((1.0f + gamma) * wsum + alpha);
                e.lambda += dLambda;
                A.Position -= dir * (dLambda * A.InverseMass);
                B.Position += dir * (dLambda * B.InverseMass);
            }

            // ── project: ground contact (normal) + compliant tread-bristle friction (brush model) ─────────────────────
            if (ground)
            {
                const Vec3 beltDisp = surfaceVelocity * h;   // how far the surface moves this substep
                // ONE compliance convention, everywhere: α is the authored compliance [m/N] and α̃ = α/Δτ² is what
                // enters the solve.  The previous code carried the tread as raw α in the stick/slide test but as
                // α/h² in the position correction two lines later, so the cone threshold and the correction it
                // guarded were computed against different stiffnesses and disagreed by a factor of Δτ².
                const float alphaTread = Parameters.TreadTangentialCompliance * invH2;
                const float gammaTread = Parameters.TreadTangentialCompliance * TreadBeta / h;
                for (SoftTyreNode& node : NodeRecords)
                {
                    if (node.InverseMass <= 0.0f) continue;
                    Vec3 surfPt{}; Vec3 normal{0, 0, 1};
                    bool onGround = ground(node.Position, surfPt, normal);
                    normal = onGround ? normal.Normalized() : Vec3{0, 0, 1};
                    const float penetration = onGround ? Dot(surfPt - node.Position, normal) : -1.0f;
                    if (penetration <= 0.0f) { node.InContact = false; continue; }   // release the bristle


                    // Normal push-out.  C = −penetration (violated while the node is below the surface), and the
                    // constraint is UNILATERAL, so the accumulated multiplier is clamped at zero: the ground may
                    // push, never pull.  Without the clamp a contact that is separating applies a suction force.
                    // Sign care: this numerator is +penetration, i.e. C = −penetration and ∇C = −n. The eq. 26
                    //    damping term −γ ∇C·Δx therefore comes out as +γ (n·Δx), not −. Getting it backwards makes
                    //    the damper PUMP the contact instead of bleeding it, which is what it did on the first try.
                    // Same correction at the ground: the constraint rate is measured against the SURFACE, which
                    // on a flat-track belt is itself moving.
                    const float cDotN = Dot(normal, (node.Position - node.Previous) - beltDisp);
                    const float dLambdaN = (penetration - alphaContact * node.ContactLambda + gammaContact * cDotN)
                                         / ((1.0f + gammaContact) * node.InverseMass + alphaContact);
                    const float newLambdaN = std::max(0.0f, node.ContactLambda + dLambdaN);
                    const float appliedN = newLambdaN - node.ContactLambda;
                    node.ContactLambda = newLambdaN;
                    node.Position += normal * (appliedN * node.InverseMass);

                    // f = λ/Δτ² is the XPBD constraint force. Use the ACCUMULATED λ, not this iteration's delta:
                    // the delta is only the increment and under-reports the force whenever the solve has already
                    // partly converged.
                    const float normalMag = std::fabs(node.ContactLambda) * invH2;
                    node.ContactPoint = surfPt;

                    // ── Tread bristle: a compliant tangential spring rooted on the ground, carried by the belt while stuck.
                    //    Its stiffness (1/α_tread) sets the slip stiffness; the μ·N cone caps it (sliding).
                    const Vec3 footPt = surfPt;   // the actual contact point, wall or floor
                    node.ContactNormal = normal;
                    if (!node.InContact) { node.BristleAnchor = footPt; node.InContact = true; }
                    else
                    {
                        // Carry the anchor with the belt, then re-seat it ONTO the surface. Pinning only .z
                        //    assumed the surface was a floor; projecting along the normal works for a wall too.
                        if (iteration == 0u) node.BristleAnchor += beltDisp;
                        node.BristleAnchor += normal * Dot(surfPt - node.BristleAnchor, normal);
                    }

                    Vec3 defl = footPt - node.BristleAnchor;             // tangential deflection
                    defl -= normal * Dot(defl, normal);
                    const float deflLen = defl.Length();
                    if (deflLen <= 1e-9f) { node.TreadDirection = Vec3{}; node.TreadLambda = 0.0f; }
                    else
                    {
                        const Vec3 tdir = defl * (1.0f / deflLen);
                        // Tangential constraint C = |deflection|, solved with the same eq. 18 as every other
                        // constraint. The resulting force is λ/Δτ², so the stick/slide test and the correction are
                        // now derived from ONE solve rather than two differently-scaled expressions.
                        const float cDotT = Dot(tdir, (node.Position - node.Previous) - beltDisp);
                        const float dLambdaT = (-deflLen - alphaTread * node.TreadLambda - gammaTread * cDotT)
                                             / ((1.0f + gammaTread) * node.InverseMass + alphaTread);
                        const float lambdaT = node.TreadLambda + dLambdaT;
                        const float fStick = std::fabs(lambdaT) * invH2;
                        const float fCone  = Parameters.FrictionCoefficient * normalMag;

                        if (fStick <= fCone)
                        {
                            // Stick: compliant correction pulls the node back toward the bristle root.
                            node.TreadLambda = lambdaT;
                            node.Position += tdir * (dLambdaT * node.InverseMass);
                            node.TreadDirection = tdir;   // friction acts along −tdir, at λ_T/Δτ²
                        }
                        else
                        {
                            // Slide (Coulomb): the multiplier saturates at the cone, and the bristle root slips so
                            // the held deflection is exactly the one the capped force supports.
                            node.TreadLambda = -fCone * h2;
                            const float heldDefl = fCone * (node.InverseMass + alphaTread) * h2;
                            node.BristleAnchor = footPt - tdir * heldDefl;
                            node.TreadDirection = tdir;
                        }
                    }
                }

            // ── project: rim bottoming (the carcass runs out of sidewall and meets the flange) ───────────────────────
            // Unilateral, like the ground: the flange may push the tread out, never pull it in. C = r − rimLimit
            // measured PERPENDICULAR to the spin axis, so camber and steer do not leak into it.
            //
            // 📝 This is the constraint that was missing. Without it the tension-only sidewall carries nothing in
            //    compression, the gas is far too soft, and the belt collapses straight through the rim — which is
            //    the spiking on landings. With it the rate goes near-vertical at the flange, which is both the
            //    correct bottoming behaviour and the reaction that stops the car.
            if (Parameters.RimBottoming)
            {
                const float alphaRim = Parameters.RimBottomingCompliance * invH2;
                const float gammaRim = Parameters.RimBottomingCompliance * RimBeta / h;
                const float rimLimit = Parameters.RimRadius + Parameters.RimBottomingClearance;
                for (SoftTyreNode& node : NodeRecords)
                {
                    if (node.InverseMass <= 0.0f) continue;
                    const Vec3  rel     = node.Position - hubPos;
                    const Vec3  radial  = rel - axisWorld * Dot(rel, axisWorld);
                    const float rLen    = radial.Length();
                    if (rLen < 1e-6f) continue;
                    const float C = rLen - rimLimit;
                    // How deep the node already was when the flange first saw it this sweep. Separating
                    //    ARRIVAL from RESIDUAL is the only way to tell an under-converged projection
                    //    (big arrival, big residual) from one that is never handed the problem at all.
                    if (-C > RimArrivalAccum) RimArrivalAccum = -C;
                    if (C >= 0.0f) { node.RimLambda = 0.0f; continue; }   // clear of the flange: carries nothing
                    const Vec3 dir = radial * (1.0f / rLen);

                    // eq. 26 again, and the hub's own travel is subtracted for the same reason it is on the
                    // spoke: the flange moves with the wheel, so the rate that matters is the node closing on
                    // the RIM, not the node moving through the world.
                    const Vec3  relPrev    = node.Previous - hubPosPrev;
                    const Vec3  radialPrev = relPrev - axisWorld * Dot(relPrev, axisWorld);
                    const float cDot = Dot(dir, (node.Position - node.Previous) - (hubPos - hubPosPrev))
                                     - (rLen - radialPrev.Length());
                    const float dLambda = (-C - alphaRim * node.RimLambda - gammaRim * cDot)
                                        / ((1.0f + gammaRim) * node.InverseMass + alphaRim);
                    const float newLambda = std::max(0.0f, node.RimLambda + dLambda);
                    const float applied   = newLambda - node.RimLambda;
                    node.RimLambda = newLambda;
                    node.Position += dir * (applied * node.InverseMass);
                }
            }
            }
        }

        // ── report the reaction from the multipliers, not from mid-sweep ─────────────────────────────────────────
        // XPBD's constraint force IS λ/Δτ², so the honest place to read it is after the substep's sweeps have
        // finished. Accumulating inside the contact pass looked equivalent and was not: as the solve
        // converges, nodes that were penetrating get pushed clear, so by the final sweep they are skipped
        // entirely and the λ they are still holding goes unreported. On the kerb rig at μ = 1.0 that lost
        // enough normal force to report a lateral reaction 2.03× the vertical one — through a friction cone
        // that cannot exceed 1.0× — while the per-node solve had never actually violated the cone.
        for (const SoftTyreNode& node : NodeRecords)
        {
            if (!(node.ContactLambda > 0.0f)) continue;

            const Vec3 normalForce = node.ContactNormal * (node.ContactLambda * invH2);
            const Vec3 rvec = node.ContactPoint - Vec3{hubPos.x, hubPos.y, node.ContactPoint.z};
            sumForce += normalForce;
            sumTorqueRef += Cross(rvec, normalForce);
            // Hub-RELATIVE, because the hub travels during the step: averaging world contact points across
            // the substeps and then comparing them to the end-of-step hub reported the patch as trailing by
            // half the step's travel — 75 mm at 36 m/s — which is the hub sweeping past, not a pneumatic trail.
            sumPatch += node.ContactPoint - hubPos; patchN += 1.0f;
            contactAccum += 1.0f;

            const Vec3 frictionForce = node.TreadDirection * (-std::fabs(node.TreadLambda) * invH2);
            sumForce += frictionForce;
            sumTorqueRef += Cross(rvec, frictionForce);
        }

        // ── velocity update, then the velocity-level pass ────────────────────────────────────────────────────────
        // PBDBodies (Müller et al., "Detailed Rigid Body Simulation with XPBD") Algorithm 2: the position solve
        //    is followed by SolveVelocities, and for contact that step is not optional.
        //
        //    Deriving velocity as (x − xprev)/h attributes the CONTACT PUSH-OUT to motion. A node that arrives
        //    below the surface — at 36 m/s and 8 substeps it can travel ~19 mm between solves, most of a 25 mm
        //    segment — is pushed back out in one go, and the divide then reports that push-out as tens of m/s
        //    of outward velocity. The node is launched, its neighbours follow through the lattice, and the
        //    carcass goes lumpy. That is exactly what the log showed: at rest the tyre was round to 0.01 mm
        //    away from the patch, but under load the off-patch spread ran to 60–130 mm while NO circumferential
        //    harmonic exceeded ~2 mm. Not a standing wave — individual nodes being flung.
        //
        //    Restitution is zero here: a tyre carcass does not bounce off the road. So any OUTWARD normal
        //    velocity on a node that is in contact is an artefact of the projection and is removed. Inward
        //    motion is left alone, because that is the tyre genuinely being loaded.
        for (SoftTyreNode& node : NodeRecords)
        {
            if (node.InverseMass <= 0.0f) { node.Velocity = {0, 0, 0}; continue; }
            node.Velocity = (node.Position - node.Previous) * (1.0f / h);

            if (node.InContact)
            {
                const float Vn = Dot(node.Velocity, node.ContactNormal);
                if (Vn > 0.0f) node.Velocity -= node.ContactNormal * Vn;   // e = 0, no bounce
            }
        }
    }

    // ── residual measurement: what is STILL violated once the solve has finished? ───────────────────────────
    // Taken after the final substep, so this is the error the frame actually ships, not a mid-solve snapshot.
    {
        ConstraintResidual = SoftTyreResidual{};
        ConstraintResidual.RimArrival = RimArrivalAccum;
        const Vec3 axisNow = hubRot.Rotate({0, 1, 0}).Normalized();
        const float spokeRest = Parameters.Radius - Parameters.RimRadius;

        double SpokeSq = 0.0, ShearSq = 0.0; uint32_t SpokeN = 0u, ShearN = 0u;
        for (const SoftTyreNode& node : NodeRecords)
        {
            if (node.InverseMass <= 0.0f) continue;
            const Vec3 anchor = hubPos + frame.Rotate(node.BeadLocal);
            const float C = (node.Position - anchor).Length() - spokeRest;
            ConstraintResidual.SpokeMax = std::max(ConstraintResidual.SpokeMax, std::fabs(C));
            SpokeSq += static_cast<double>(C) * C; ++SpokeN;

            const Vec3 restWorld = hubPos + frame.Rotate(node.TreadLocal);
            const Vec3 offset = node.Position - restWorld;
            Vec3 radial = restWorld - hubPos;
            radial = radial - axisNow * Dot(radial, axisNow);
            const float rl = radial.Length();
            if (rl > 1e-6f)
            {
                radial = radial * (1.0f / rl);
                const Vec3 tangent = Cross(axisNow, radial);
                const float Ct = Dot(offset, tangent), Ca = Dot(offset, axisNow);
                const float Cs = std::sqrt(Ct * Ct + Ca * Ca);
                ConstraintResidual.ShearMax = std::max(ConstraintResidual.ShearMax, Cs);
                ShearSq += static_cast<double>(Cs) * Cs; ++ShearN;
            }
        }
        if (SpokeN) ConstraintResidual.SpokeRms = std::sqrt(static_cast<float>(SpokeSq / SpokeN));
        if (ShearN) ConstraintResidual.ShearRms = std::sqrt(static_cast<float>(ShearSq / ShearN));

        double Sq[3] = {0.0, 0.0, 0.0}; uint32_t Cnt[3] = {0u, 0u, 0u};
        float Mx[3] = {0.0f, 0.0f, 0.0f};
        for (const Edge& e : ConstraintEdges)
        {
            const float C = (NodeRecords[e.b].Position - NodeRecords[e.a].Position).Length() - e.rest;
            const uint32_t F = static_cast<uint32_t>(e.family);
            Mx[F] = std::max(Mx[F], std::fabs(C));
            Sq[F] += static_cast<double>(C) * C; ++Cnt[F];
        }
        ConstraintResidual.HoopMax = Mx[0]; ConstraintResidual.LateralMax = Mx[1]; ConstraintResidual.DiagonalMax = Mx[2];
        if (Cnt[0]) ConstraintResidual.HoopRms     = std::sqrt(static_cast<float>(Sq[0] / Cnt[0]));
        if (Cnt[1]) ConstraintResidual.LateralRms  = std::sqrt(static_cast<float>(Sq[1] / Cnt[1]));
        if (Cnt[2]) ConstraintResidual.DiagonalRms = std::sqrt(static_cast<float>(Sq[2] / Cnt[2]));

        if (ground)
            for (const SoftTyreNode& node : NodeRecords)
            {
                Vec3 sp{}; Vec3 nrm{0, 0, 1};
                if (!ground(node.Position, sp, nrm)) continue;
                const float pen = Dot(sp - node.Position, nrm.Normalized());
                if (pen > ConstraintResidual.ContactMax) ConstraintResidual.ContactMax = pen;
            }

        if (Parameters.RimBottoming)
        {
            const float rimLimit = Parameters.RimRadius + Parameters.RimBottomingClearance;
            for (const SoftTyreNode& node : NodeRecords)
            {
                const Vec3  rel    = node.Position - hubPos;
                const float rLen   = (rel - axisWorld * Dot(rel, axisWorld)).Length();
                const float breach = rimLimit - rLen;
                if (breach > ConstraintResidual.RimMax) ConstraintResidual.RimMax = breach;
            }
        }
    }

    const float inv = 1.0f / static_cast<float>(substeps);
    ContactReaction.Force = sumForce * inv;
    ContactReaction.Mz = sumTorqueRef.z * inv;   // vertical component about the hub ground projection
    ContactReaction.ContactCount = static_cast<uint32_t>(contactAccum * inv + 0.5f);
    ContactReaction.PatchCentre = (patchN > 0.0f) ? motion.Position + sumPatch * (1.0f / patchN) : hubPos;
    ContactReaction.EnclosedVolume = gasVolume;
    ContactReaction.GaugePressure  = gasGauge;
    Vec3 inflation{};
    for (const Vec3& gradient : InflationGradient) inflation += gradient * gasGauge;
    ContactReaction.InflationForce = inflation;
}

} // namespace Frontier::Vehicle
