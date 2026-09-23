# Plan — bounded partial unequal-radius bicone apex toroidal fillet

## Capability slice

Add the next distinct combination route after Phase 39a: one strict non-reflex partial sector of
an unequal-radius, coaxial right-circular bicone whose sharp apex is replaced by one analytic
constant-radius toroidal fillet. The torus is a surface of revolution of the exact tangent
three-point meridian arc; both finite sector ends receive planar radial caps.

The distinct fixture uses lower/upper radii 4.5/2.7, heights 6.8/4.6, a 95-degree sweep, and a
fillet radius of 0.62.

## Source and reconstruction contract

The classifier accepts the same capped native partial-bicone source topology as Phase 39a,
`V7/E11/C22/L6/F6`, but exposes a separate fillet specification and separate reconstruction route.
It derives the apex, canonical coaxial axis, unequal support dimensions, and native sweep without
mutating the source. The route remains independent of the full-turn unequal/equal bicone fillets
and the partial unequal chamfer.

The reconstruction solves the two cone tangent contacts from the support half-angles, revolves the
lower cone, torus meridian, upper cone, and two planar base caps over the finite sweep, closes the
same two radial ends, and returns a closed genus-zero `V10/E15/C30/L7/F7` solid. Its volume is the
full-turn cone/torus meridian identity scaled by `sweep / (2 pi)`.

## Refusal boundary

Only a canonical strict non-reflex partial sector is accepted. Full turns, half/reflex sectors,
equal radii, mixed cone/plane/cylinder apexes, oblique/non-coaxial or freeform supports,
variable-radius laws, arbitrary selections, invalid/consuming fillet radii, and healing remain
refused. The earlier partial chamfer remains a separate route.

## Verification and proof

`PartialUnequalBiconeApexFilletVerification` builds the source independently, checks exact sweep and
support extraction, analytic torus contacts, topology, radial-cap closure, normals, sector volume,
source immutability, and refusal boundaries. The durable proof is
`Proofs/Phase39b_PartialUnequalBiconeApexFillet.png`.
