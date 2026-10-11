//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/XPBDSoftTyre.h — calibrated multi-ring XPBD soft-body tyre (Phase 2)
//============================================================================================================================================
//
//    Phase-2 evolution of the Phase-0 `XPBDTyreSolver` skeleton (single ring) into a physically-calibrated multi-ring
//    carcass. The tyre is the one deformable element in the vehicle (the user's explicit call: tyres are rigid-XPBD soft
//    bodies, everything else stays rigid in Jolt). Following Müller et al. 2020 "Detailed Rigid Body Simulation with XPBD"
//    and the XPBD small-steps formulation (Macklin et al. 2019), the carcass is a cylindrical lattice of particles held
//    together by compliant distance constraints and inflated by a gauge-pressure body force:
//
//        • radial spokes   node ↔ rim(bead) anchor          — sidewall radial stiffness
//        • hoop            node ↔ next segment (same ring)   — tread-band circumferential stiffness
//        • lateral         node ↔ same segment (next ring)   — carcass lateral stiffness
//        • shear/diagonal  node ↔ next-segment-next-ring     — patch shear stiffness (this is what lets a slip angle
//                                                              build a lateral force and an asymmetric patch → Mz)
//        • inflation       outward radial body force P·A     — the primary vertical load carrier
//        • contact         inequality vs ground + Coulomb friction against a (possibly moving) surface velocity
//
//    Emergent behaviour: a loaded, rolling carcass with contact friction is a physical brush model. Longitudinal/lateral
//    slip deform the sticking contact patch and produce Fx/Fy that rise, peak, and saturate — the Magic-Formula shape —
//    plus a pneumatic-trail self-aligning torque. XPBDTyreValidation.cpp calibrates the handful of constitutive
//    parameters against the Phase-1 Pacejka baseline and reports the residual agreement.
//
//    Pure C++/scalar, self-contained vector math — no Unreal, no Jolt, no threading (like the other Phase-1 modules), so
//    it compiles and is validated headless in the sandbox. In the engine it is driven by the Phase-0 physics thread and
//    reads the same Jolt heightfield the rest of the sim uses (via the GroundQuery hook).

#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace Frontier::Vehicle {

//------------------------------------------------------------------------------------------------------------------------
//                                              minimal vector / quaternion
//------------------------------------------------------------------------------------------------------------------------
struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float X, float Y, float Z) noexcept : x(X), y(Y), z(Z) {}

    Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s)       const noexcept { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }

    [[nodiscard]] float Length()    const noexcept { return std::sqrt(x * x + y * y + z * z); }
    [[nodiscard]] float LengthSq()  const noexcept { return x * x + y * y + z * z; }
    [[nodiscard]] Vec3  Normalized() const noexcept
    {
        const float l = Length();
        return (l > 1e-12f) ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0};
    }
};

[[nodiscard]] inline float Dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] inline Vec3  Cross(const Vec3& a, const Vec3& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Quaternion (x,y,z,w). Only rotation-of-vector and axis-angle construction are needed.
struct Quat
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    [[nodiscard]] static Quat AxisAngle(const Vec3& axis, float angleRad) noexcept
    {
        const Vec3 a = axis.Normalized();
        const float h = 0.5f * angleRad, s = std::sin(h);
        return {a.x * s, a.y * s, a.z * s, std::cos(h)};
    }
    // Hamilton product. (a ∘ b).Rotate(v) == a.Rotate(b.Rotate(v)) — b is applied first, so the carcass's own
    // spin composes on the RIGHT of the hub frame: it is a rotation of the material, inside the hub's axes.
    [[nodiscard]] Quat operator*(const Quat& o) const noexcept
    {
        return { w * o.x + x * o.w + y * o.z - z * o.y,
                 w * o.y - x * o.z + y * o.w + z * o.x,
                 w * o.z + x * o.y - y * o.x + z * o.w,
                 w * o.w - x * o.x - y * o.y - z * o.z };
    }
    [[nodiscard]] Quat Normalized() const noexcept
    {
        const float l = std::sqrt(x * x + y * y + z * z + w * w);
        return (l > 1e-12f) ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
    [[nodiscard]] Vec3 Rotate(const Vec3& v) const noexcept
    {
        const Vec3 u{x, y, z};
        const Vec3 t = Cross(u, v) * 2.0f;
        return v + (t * w) + Cross(u, t);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARAMETERS
//------------------------------------------------------------------------------------------------------------------------
struct SoftTyreParameters
{
    // Geometry
    float    Radius       = 0.34f;   // [m] unloaded tread radius
    float    RimRadius    = 0.20f;   // [m] bead/rim radius (spoke inner anchor)
    float    Width        = 0.245f;  // [m] section width
    uint32_t RingCount    = 9u;      // [-] rings across the width (≥ 2)
    uint32_t SegmentCount = 128u;    // [-] particles per ring around the circumference (≥ 8)
    float    TotalMass    = 12.0f;   // [kg] carcass mass over all particles
    // Gauss-Seidel sweeps inside each substep. See the comment on the sweep loop in Step: one sweep leaves
    // the ground reaction — the only number the vehicle consumes — short of the lattice's own elastic state,
    // and the shortfall is a function of the schedule, which is the very dependence XPBD exists to remove.
    uint32_t SolverIterations = 1u;  // [-]  sweeps per substep

    // ── Inflation: a real gas inside a closed surface ───────────────────────────────────────────────────────
    // This used to be a per-node body force P·A along the outward radial, with A the node's REST patch area.
    // It was measured and it does not carry load. Summed over a closed ring those forces cancel to within a
    // rounding error: at 40 mm of squash the vertical component of the whole inflation field was +60 N against
    // a 37.4 kN ground reaction — 0.16%. Every newton the tyre carried came from compressing the sidewall
    // spokes, so `InflationPressure` was very nearly an inert number and the carcass was a spring mattress
    // with a rigid rim, not a pneumatic tyre.
    //
    // The load path a real tyre uses is the gas pushing on the RIM: over the flattened contact patch the
    // pressure that would have pushed the tread down is missing, so the net gas force on the wheel is p·A_patch
    // upward. That path only exists if the pressure is integrated over the ENCLOSED SURFACE — tread band, both
    // sidewalls, and the rim barrel that closes it. The force on a node is then the exact geometric gradient
    //
    //     f_i = p ∂V/∂x_i,     V = ⅙ Σ_tri x₀·(x₁ × x₂),     ∂V/∂x₀ = ⅙ (x₁ × x₂)
    //
    // which is p × (the area-weighted normal of the surface that node carries) in the DEFORMED configuration.
    // It reproduces p·A on every patch, and because V is translation-invariant the forces on the free nodes sum
    // to exactly minus the force on the kinematic rim — the missing load path, now present.
    float    InflationPressure    = 110000.0f;   // [Pa] gauge pressure at the rest volume
    float    AtmosphericPressure  = 101325.0f;   // [Pa] outside pressure — sets how the gas law stiffens
    // Boyle's law at constant temperature: p_abs·V = p_abs0·V0. Squashing the carcass shrinks V, so the gauge
    // pressure RISES, and the tyre stiffens progressively the way an air spring does. A tyre whose gas is
    // modelled as a fixed pressure has no bottoming-out behaviour at all.
    bool     GasCompressible      = true;        // [-]  false pins the gauge pressure at InflationPressure

    // Compliances [m/N]  (α = 1/stiffness). Lower ⇒ stiffer. Values below are the Phase-2 calibration (see
    // XPBDTyreValidation.cpp) that best-matches the Phase-1 Pacejka baseline at ~5 kN load.
    float    SpokeCompliance   = 5.0e-5f;  // sidewall radial (soft enough for a multi-node contact patch)
    // A sidewall is a cord membrane. It pulls and it cannot push: load it in compression and it BUCKLES, which
    // is why a flat tyre collapses rather than standing on its sidewalls. The radial spoke is therefore
    // tension-only — slack whenever the tread node sits closer to the rim than its rest radius. Left
    // bilateral, the sidewall takes the whole contact patch in compression and the inflation pressure is
    // bypassed; that is exactly what the measurement above found.
    bool     SidewallTensionOnly = true;   // [-] false restores the old bilateral (run-flat) sidewall
    // The sidewall also resists TANGENTIAL and AXIAL motion, and until now nothing modelled that. The spoke
    // was a bare distance constraint: it pinned |node − bead| and said nothing about direction, so the whole
    // tread band was free to slide bodily around on its constraint spheres. Measured: the lattice centroid
    // drifted up to 14.6 mm from the hub at speed, and the nodes' radii about the hub then scattered by ~13 mm
    // RMS even though the band itself was still round about its own centroid. A real tyre is soft radially and
    // an order of magnitude stiffer in shear -- that is what a belted carcass IS -- so these are much lower.
    float    SpokeTangentialCompliance = 2.0e-5f;  // belt wind-up about the axle
    float    SpokeLateralCompliance    = 2.0e-5f;  // sidewall lateral (steer/camber path)
    float    SpokeShearDampingRatio    = 0.90f;
    float    HoopCompliance    = 4.0e-7f;  // tread-band circumferential
    float    LateralCompliance = 3.0e-7f;  // carcass lateral
    float    ShearCompliance   = 5.0e-7f;  // diagonal shear (patch)
    float    ContactCompliance = 1.0e-7f;  // ground contact (normal)
    // Tread tangential (bristle) compliance — sets the slip STIFFNESS (initial slope of Fx/κ, Fy/α). This is the
    // rubber-tread shear that lets the contact force build gradually before the cone limit, exactly like a brush model.
    float    TreadTangentialCompliance = 9.0e-7f;
    // ── Rim bottoming ───────────────────────────────────────────────────────────────────────────────────────
    // A tyre runs out of sidewall. Past that the carcass is pinched between the road and the rim flange and the
    //    rate goes almost vertical — that hard stop is what keeps a landing from driving the wheel through the
    //    road, and it is why a bottomed tyre thumps instead of swallowing the bump.
    //
    //    ⚠️ Nothing modelled it. The radial spoke is tension-only (it buckles, correctly), the gas is far too
    //    soft to stand in for steel, and no constraint named the rim at all, so the tread simply kept
    //    collapsing: measured on a flat-ground sweep, a 160 mm deflection put the tread band at 180 mm radius
    //    against a 200 mm rim — 20 mm INSIDE the wheel — and 180 mm put it 40 mm inside. The belt passing
    //    through its own rim is the spiking seen on landings, and Fz stayed linear throughout, so the car was
    //    never given the reaction that should have stopped it.
    //
    //    The constraint is unilateral and stiff: a node may not approach the spin axis closer than
    //    RimRadius + RimBottomingClearance. The clearance is the tread and carcass thickness that is still
    //    there to be squeezed when the belt reaches the flange.
    bool     RimBottoming            = true;      // [-]   false restores the old unbounded collapse
    float    RimBottomingClearance   = 0.015f;    // [m]   tread + carcass thickness held off the flange
    float    RimBottomingCompliance  = 2.0e-8f;   // [m/N] rubber pinched on steel: stiffer than ground contact
    // 📝 Quarter-critical, and that number is measured rather than assumed. XPBD's damped solve carries γ in
    //    the denominator (Δλ = (−C − α̃λ − γĊ)/((1+γ)w + α̃)), so the fraction of a breach a sweep can recover
    //    is 1/(1+γ) — at ζ = 1.0 that is 95%, and ground contact re-drives the node every sweep, so the
    //    leftover 5% never converges away however many sweeps are spent (measured: 2.40 → 1.73 → 1.63 mm for
    //    1, 2, 3 sweeps, with the residual holding at ~5% of arrival throughout). On the kerb strike that left
    //    the tread 2.40 mm inside the flange. Dropping ζ moves it in proportion — 1.28 mm at 0.5, 0.67 mm at
    //    0.25, 0.29 mm at 0.1 — and the dissipation the high ratio was there to provide does not show up in
    //    any measurement: peak Fz is 30.3 kN at every ratio, and the carcass ringing metric has no trend
    //    (3218 / 3617 / 3859 / 3026 mm/s), with ζ = 1.0 actually the worst on off-patch spread. So the heavy
    //    ratio was buying nothing and costing position accuracy. 0.25 keeps real damping and puts the kerb
    //    strike (0.67 mm) on the same footing as the ramp landing (0.72 mm).
    float    RimBottomingDampingRatio = 0.25f;    // [-]   a pinch dissipates, but not at the cost of holding

    // ── Rayleigh damping, as DAMPING RATIOS ─────────────────────────────────────────────────────────────────
    // XPBD §5. The elastic solve alone dissipates only what the implicit discretisation happens to lose, which
    // for a 1152-node pressurised lattice is nowhere near enough: the carcass rings, and that ring is visible
    // as the tyre wobbling under a car that is otherwise driving correctly. The paper folds a Rayleigh
    // dissipation potential D = ½ Ċᵀ β Ċ into the same Gauss-Seidel update (eq. 26):
    //
    //     Δλ = (−C − α̃λ − γ ∇C·(x − xⁿ)) / ((1 + γ) Σw|∇C|² + α̃),      γ = α̃ β̃ / Δt,  β̃ = Δt² β
    //
    // so γ reduces to α·β/Δt.
    //
    // These are RATIOS (ζ), not β itself, and β is derived at Build time as β = 2ζ√(k·m) with k = 1/α and m the
    // per-node mass. Authoring β directly was a mistake worth recording: the first attempt hand-picked values
    // like β_hoop = 14 N·s/m and commented them "ζ ≈ 0.9", but the real critical coefficient for that
    // constraint is 2√(k·m) = 2√(2.5e6 × 0.0104) ≈ 322, so the tyre was running at ζ ≈ 0.04 — essentially
    // undamped — and went on ringing. A ratio cannot drift out of step with the stiffness it damps.
    float    SpokeDampingRatio   = 0.70f;   // sidewall radial
    float    HoopDampingRatio    = 0.90f;   // tread-band hoop — this is the visible ringing mode
    float    LateralDampingRatio = 0.90f;   // carcass lateral
    float    ShearDampingRatio   = 0.90f;   // diagonal shear
    float    TreadDampingRatio   = 0.80f;   // tread bristle
    // Contact stays UNDAMPED, as in the paper: XPBD §6 assumes zero compliance in contact and stores no
    // multiplier for it. Damping a near-rigid unilateral constraint mostly fights the push-out, and measurably
    // made the settled load schedule-dependent when tried.
    float    ContactDampingRatio = 0.0f;

    // Friction
    float    FrictionCoefficient = 2.2f;   // Coulomb μ (calibrated so emergent peak ≈ Pacejka D-factor at the ~0.6
                                           // patch-utilisation of this mesh resolution; see the validation report)

    Vec3     Gravity = {0.0f, 0.0f, -9.81f};   // Frontier is +Z up; spin axis is +Y; longitudinal is +X
};

//------------------------------------------------------------------------------------------------------------------------
struct SoftTyreNode
{
    Vec3  Position, Previous, Velocity;
    Vec3  TreadLocal;   // rest tread position in the carcass's MATERIAL frame (rotates with the wheel)
    Vec3  BeadLocal;    // rim/bead anchor in the same material frame
    float InverseMass = 0.0f;
    // Brush/bristle state for the compliant tread friction.
    Vec3  BristleAnchor{};      // world root of the tread bristle on the ground (carried by the belt while stuck)
    bool  InContact = false;    // was this node in contact on the previous substep (bristle alive)?
    Vec3  ContactNormal{0.0f, 0.0f, 1.0f};   // surface normal where it touched, for the velocity pass
    Vec3  ContactPoint{};       // surface point it was resolved against, for the torque arm and patch centre
    Vec3  TreadDirection{};     // unit direction of the bristle deflection; friction acts along −this

    // ── XPBD Lagrange multipliers ────────────────────────────────────────────────────────────────────────────
    // Macklin et al., "XPBD: Position-Based Simulation of Compliant Constrained Dynamics", eq. 18:
    //
    //     Δλ = (−C − α̃ λ) / (Σ wᵢ|∇Cᵢ|² + α̃)          α̃ = α / Δt²
    //     Δxᵢ = wᵢ ∇Cᵢ Δλ,      λ ← λ + Δλ
    //
    // λ MUST persist across the iterations of a substep and reset at the start of the next one.  Dropping it
    // (and with it the −α̃λ term) degrades XPBD back to PBD: the effective stiffness then depends on iteration
    // and substep count, which is the exact defect XPBD was published to remove.  These are the accumulators.
    float SpokeLambda   = 0.0f;   // sidewall radial constraint
    float SpokeTangentialLambda = 0.0f;   // sidewall tangential (belt wind-up)
    float SpokeLateralLambda    = 0.0f;   // sidewall axial
    float ContactLambda = 0.0f;   // ground non-penetration (unilateral: λ ≥ 0)
    float TreadLambda   = 0.0f;   // tread bristle tangential constraint
    float RimLambda     = 0.0f;   // rim-flange bottoming (unilateral: λ ≥ 0)
};

// ── per-constraint-family residual, measured AFTER the solve ────────────────────────────────────────────────
// Every structural hypothesis about the residual carcass scatter has been eliminated by measurement (contact
// patch, standing wave, outliers, profile, push-out velocity, friction, substep count, measurement centre,
// band drift). What has never been looked at directly is the obvious thing: WHICH constraint is actually left
// violated at the end of a step, and by how much. A constraint family with a large residual is one the solver
// is failing to satisfy; that is a far better lead than another parameter sweep.
//
// C is in metres for every family here, so the numbers are directly comparable.
struct SoftTyreResidual
{
    float SpokeMax = 0.0f,   SpokeRms = 0.0f;      // sidewall radial
    float ShearMax = 0.0f,   ShearRms = 0.0f;      // sidewall tangential + axial
    float HoopMax = 0.0f,    HoopRms = 0.0f;       // tread band circumferential
    float LateralMax = 0.0f, LateralRms = 0.0f;    // carcass lateral
    float DiagonalMax = 0.0f,DiagonalRms = 0.0f;   // diagonal shear
    float ContactMax = 0.0f;                       // deepest remaining penetration
    float RimMax = 0.0f;                           // deepest remaining rim-flange interference
    float RimArrival = 0.0f;                       // deepest interference the projection was HANDED this step
};

// Aggregate ground-on-tyre reaction over the last Step (the force the tyre transmits to the car).
struct TyreReaction
{
    Vec3  Force{};        // Fx (long), Fy (lat, along −spin-axis dir), Fz (vertical)
    float Mz = 0.0f;      // self-aligning torque about vertical through the contact centre
    uint32_t ContactCount = 0u;
    Vec3  PatchCentre{};  // world centre of the contact patch
    float EnclosedVolume = 0.0f;   // [m³] gas volume at the end of the step
    float GaugePressure  = 0.0f;   // [Pa] gauge pressure the gas law produced from that volume
    // Net gas force on the free carcass nodes. For a closed surface this is exactly minus the gas force on
    // the kinematic rim, so its vertical component IS the pneumatic load path — p·A_patch when the patch is
    // flat, and a direct check that the inflation is carrying the tyre rather than the sidewall.
    Vec3  InflationForce{};        // [N]
};

//------------------------------------------------------------------------------------------------------------------------
//                                           THE HUB IS A KINEMATIC BODY
//------------------------------------------------------------------------------------------------------------------------
//    The hub used to enter Step as one frozen pose held for the whole frame, and the carcass never rotated at
//    all: the wheel's spin lived in VehicleSolver's Pacejka layer and was never handed to the soft body, so
//    the road was faked as a belt sliding under a stationary sock.
//
//    Both are measurable defects rather than simplifications. Frozen pose: at 36 m/s and a 240 Hz step the
//    anchors teleport 150 mm — nine node spacings — between frames, and the whole lattice is yanked after them
//    in a single substep. Measured on a flat floor at 30 mm of commanded squash, the shipped path reached
//    318 mm of squash and 138 mm RMS node scatter at 36 m/s, i.e. it had come apart; walking the same hub
//    across the substeps held it at 60 mm. No spin: the contact patch is forever the same material nodes, so
//    there is no rolling, no centrifugal growth, no standing wave, and a procedural tread pattern would sit
//    still while the car drove.
//
//    The hub is therefore an ordinary XPBD kinematic body: a pose at the END of the step, plus the velocities
//    needed to walk it backwards through the substeps.
struct HubMotion
{
    Vec3  Position{};            // [m]     hub centre at the END of this step
    Quat  Orientation{};         // [-]     hub frame at the END of this step — steer and camber, never spin
    Vec3  LinearVelocity{};      // [m/s]   hub translation rate, walks the kinematic frame across the substeps
    float SpinRate = 0.0f;       // [rad/s] wheel rotation about hub-local +Y (+ = rolling toward +X)
};

//------------------------------------------------------------------------------------------------------------------------
class XPBDSoftTyre
{
public:
    // ── the ground contract: a SURFACE query, not a heightfield ─────────────────────────────────────────────
    // This used to be bool(p, float& outGroundZ, Vec3& outNormal) -- one height per (x, y) column. That cannot
    // express a vertical face. Against a kerb a node beside the wall reads the LOW ground and passes straight
    // through it, and a node over the kerb's footprint is pushed UP instead of being blocked sideways. No
    // amount of extra bristles helps, because the query they consult has nowhere to report a side wall.
    //
    // It now returns the nearest point ON THE SURFACE plus its outward normal. The solver already computed
    // penetration as Dot(surfacePoint - position, normal), so the contact, friction and reaction code is
    // unchanged -- it was only ever the QUERY that was flattened. A heightfield implementation stays trivial:
    // return {p.x, p.y, Height(p.x, p.y)}.
    using GroundQuery = std::function<bool(const Vec3& p, Vec3& outSurfacePoint, Vec3& outNormal)>;

    void Build(const SoftTyreParameters& params, const Vec3& hubPos, const Quat& hubRot) noexcept;

    // Advance by dt using `substeps` XPBD substeps against a kinematic hub that TRANSLATES and SPINS across
    // them. `surfaceVelocity` is the velocity of the ground surface itself — {0,0,0} for a road, non-zero only
    // for a flat-track belt rig.
    void Step(float dt, uint32_t substeps, const HubMotion& hub,
              const Vec3& surfaceVelocity, const GroundQuery& ground) noexcept;

    // A hub held still: the settle, kerb and calibration rigs, where the wheel neither travels nor rolls.
    void Step(float dt, uint32_t substeps, const Vec3& hubPos, const Quat& hubRot,
              const Vec3& surfaceVelocity, const GroundQuery& ground) noexcept
    {
        Step(dt, substeps, HubMotion{hubPos, hubRot, Vec3{}, 0.0f}, surfaceVelocity, ground);
    }

    [[nodiscard]] const std::vector<SoftTyreNode>& Nodes() const noexcept { return NodeRecords; }
    [[nodiscard]] const SoftTyreParameters&        Params() const noexcept { return Parameters; }
    [[nodiscard]] const TyreReaction&              Reaction() const noexcept { return ContactReaction; }
    [[nodiscard]] const SoftTyreResidual&          Residual() const noexcept { return ConstraintResidual; }
    [[nodiscard]] bool Constructed() const noexcept { return !NodeRecords.empty(); }

    [[nodiscard]] uint32_t Index(uint32_t ring, uint32_t seg) const noexcept { return ring * Parameters.SegmentCount + seg; }

    // The carcass's own rotation, accumulated from SpinRate. A procedural tread pattern is authored in the
    // material frame and posed with this, so the pattern rolls with the wheel instead of standing still.
    [[nodiscard]] float CarcassAngle() const noexcept { return SpinAngle; }
    [[nodiscard]] Quat  MaterialFrame(const Quat& hubRot) const noexcept
    {
        return (hubRot * Quat::AxisAngle(Vec3{0.0f, 1.0f, 0.0f}, SpinAngle)).Normalized();
    }
    [[nodiscard]] float RestVolume() const noexcept { return GasRestVolume; }

private:
    // `lambda` is the edge's XPBD multiplier, reset at the top of every substep (see SoftTyreNode).
    // Family tags the edge so residuals can be reported per constraint type rather than as one blur.
    enum class EdgeFamily : uint32_t { Hoop = 0u, Lateral = 1u, Diagonal = 2u };
    struct Edge { uint32_t a, b; float rest, compliance, damping; EdgeFamily family; float lambda = 0.0f; };

    // One triangle of the closed gas surface. A vertex index below NodeRecords.size() is a free tread node;
    // anything above it is a kinematic bead vertex, which carries volume but takes no force.
    struct SurfaceTriangle { uint32_t a, b, c; };

    void BuildEdges() noexcept;
    void BuildGasSurface() noexcept;

    // Rebuilds the bead vertices from the hub frame, measures the enclosed volume, and leaves ∂V/∂x in
    // InflationGradient. Returns the volume.
    [[nodiscard]] float MeasureInflation(const Vec3& hubPos, const Quat& frame) noexcept;

    // β = 2ζ√(k·m), k = 1/compliance, m = per-node mass. Derived once in Build so the ratio and the stiffness
    //    can never fall out of step.
    [[nodiscard]] float DerivedDamping(float compliance, float ratio) const noexcept;

    float SpokeBeta = 0.0f, ContactBeta = 0.0f, TreadBeta = 0.0f, RimBeta = 0.0f;
    float SpokeTangentialBeta = 0.0f, SpokeLateralBeta = 0.0f;   // the non-edge constraints' derived β

    float SpinAngle     = 0.0f;   // [rad] carcass rotation about hub-local +Y, accumulated across steps
    float GasRestVolume = 0.0f;   // [m³]  enclosed volume of the undeformed carcass

    SoftTyreParameters            Parameters;
    std::vector<SoftTyreNode>     NodeRecords;
    std::vector<Edge>             ConstraintEdges;
    std::vector<SurfaceTriangle>  GasSurface;          // closed: tread band + both sidewalls + rim barrel
    std::vector<Vec3>             BeadLocalVertices;   // material-frame rim vertices (two circles)
    std::vector<Vec3>             BeadWorldVertices;   // the same, posed by the hub this substep
    std::vector<Vec3>             InflationGradient;   // ∂V/∂x per free node [m²]
    TyreReaction                  ContactReaction;
    SoftTyreResidual              ConstraintResidual;
};

} // namespace Frontier::Vehicle
