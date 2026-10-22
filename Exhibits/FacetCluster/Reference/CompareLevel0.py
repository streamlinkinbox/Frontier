#!/usr/bin/env python3
# ==========================================================================================================================================
#  CompareLevel0.py - compare the native K1 partition dump against the HTML reference's level-0 clusters.
#
#  Both inputs are JSON objects keyed by corpus name. Each cluster carries first facet, facet count, enclosure box, sphere and
#  cone, so this compares membership *and* geometry rather than counts alone.
#
#  Usage: python3 Exhibits/FacetCluster/Reference/CompareLevel0.py <reference.json> <native dump.json>
# ==========================================================================================================================================
import json
import sys

TOLERANCE = 1.0e-9


def Compare(Name, Reference, Native):
    Failures = []
    if Reference["vertices"] != Native["vertices"]:
        Failures.append(f"welded vertices {Native['vertices']} != reference {Reference['vertices']}")
    if Reference["sharp"] != Native["sharp"]:
        Failures.append(f"sharp edges {Native['sharp']} != reference {Reference['sharp']}")
    if Reference["uvseam"] != Native["uvseam"]:
        Failures.append(f"uv seams {Native['uvseam']} != reference {Reference['uvseam']}")

    Left, Right = Reference["clusters"], Native["clusters"]
    if len(Left) != len(Right):
        Failures.append(f"cluster count {len(Right)} != reference {len(Left)}")

    Worst = dict.fromkeys(("centre", "radius", "axis", "cutoff", "minimum", "maximum"), 0.0)
    Mismatched = 0
    for Identity, (Theirs, Ours) in enumerate(zip(Left, Right)):
        if Theirs["first"] != Ours["first"] or Theirs["count"] != Ours["count"]:
            Mismatched += 1
            continue
        Worst["radius"] = max(Worst["radius"], abs(Theirs["radius"] - Ours["radius"]))
        Worst["cutoff"] = max(Worst["cutoff"], abs(Theirs["cutoff"] - Ours["cutoff"]))
        for Field in ("centre", "axis", "minimum", "maximum"):
            for TheirComponent, OurComponent in zip(Theirs[Field], Ours[Field]):
                Worst[Field] = max(Worst[Field], abs(TheirComponent - OurComponent))
    if Mismatched:
        Failures.append(f"{Mismatched} clusters have different facet membership")
    for Field, Offset in Worst.items():
        if Offset > TOLERANCE:
            Failures.append(f"{Field} differs by {Offset:.3e} (tolerance {TOLERANCE:g})")

    print(f"  {Name}: {len(Right)} clusters, {Native['vertices']} welded vertices, "
          f"{Mismatched} membership mismatches")
    print("    max |delta| vs reference  " + "  ".join(f"{Field}={Offset:.3e}" for Field, Offset in Worst.items()))
    for Failure in Failures:
        print(f"    ! {Failure}")
    return not Failures


def Main():
    if len(sys.argv) != 3:
        print(__doc__ if __doc__ else "usage: CompareLevel0.py <reference.json> <native.json>")
        return 2
    Reference = json.load(open(sys.argv[1], encoding="utf-8"))
    Native = json.load(open(sys.argv[2], encoding="utf-8"))
    Shared = [Name for Name in ("sphere", "heightfield") if Name in Reference and Name in Native]
    if not Shared:
        print("  no shared corpora between the two dumps")
        return 2
    print("Reference parity - native K1 against HTML GeometryHierarchy.js")
    Passed = all([Compare(Name, Reference[Name], Native[Name]) for Name in Shared])
    print(f"  result {'PASS' if Passed else 'FAIL'}")
    return 0 if Passed else 1


if __name__ == "__main__":
    sys.exit(Main())
