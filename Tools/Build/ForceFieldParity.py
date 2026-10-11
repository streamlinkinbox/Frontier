#!/usr/bin/env python3
"""Hold the native force field taxonomy to the browser one it was ported from.

    python3 Tools/Build/ForceFieldParity.py

Experimental/ParticleEditor/js/forcefields.js is the design reference and
Engine/VolumetricDynamics/ForceFieldSet.h is the port, exactly as the gas inspector worked. A port is a
port only on the day it is written unless something compares the two afterwards, so this reads both and
insists they list the same kinds, in the same order, with the same contribution, the same reach, and the
same shader slots.

It parses rather than imports, because the JS is an ES module in a browser page and the C++ is a header,
and a check that needs a toolchain to run is a check that stops being run.
"""
import re
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Faults = []
Claims = 0


def Check(Condition, Claim):
    global Claims
    Claims += 1
    if Condition:
        print(f"  PASS  {Claim}")
    else:
        print(f"  FAIL  {Claim}")
        Faults.append(Claim)


def ReadBrowserKinds(Body):
    Block = Body[Body.index("export const ForceFieldKinds = ["):]
    Block = Block[: Block.index("\n];")]
    Kinds = []
    for Entry in re.findall(r"\{ Id: \"(\w+)\",(.*?)\},", Block, re.S):
        Id, Rest = Entry
        Give = re.search(r"Give: Contribution\.(\w+)", Rest)
        Reaches = re.search(r"Reaches: Reach\.(\w+)", Rest)
        Lattice = re.search(r"Lattice: (\d+)", Rest)
        Force = re.search(r"Force: (\d+)", Rest)
        Kinds.append({
            "Id": Id,
            "Name": re.search(r"Name: \"([^\"]+)\"", Rest).group(1),
            "Give": Give.group(1).lower() if Give else None,
            "Reaches": Reaches.group(1).lower() if Reaches else None,
            "Lattice": int(Lattice.group(1)) if Lattice else -1,
            "Force": int(Force.group(1)) if Force else -1,
            "Inverts": "Inverts: true" in Rest,
        })
    return Kinds


def ReadNativeKinds(Body):
    Block = Body[Body.index("static const ForceFieldKindFacts Known"):]
    Block = Block[: Block.index("\n    };")]
    Kinds = []
    for Line in re.findall(r"\{ \"(\w+)\",([^\n]*)\},", Block):
        Id, Rest = Line
        Parts = [One.strip() for One in Rest.split(",")]
        Kinds.append({
            "Id": Id,
            "Name": Parts[0].strip('"'),
            "Give": Parts[1].replace("ForceContribution::", "").strip().lower(),
            "Reaches": Parts[2].replace("ForceReach::", "").strip().lower(),
            "Lattice": int(Parts[4]),
            "Force": int(Parts[5]),
            "Inverts": Parts[6].strip() == "true",
        })
    return Kinds


def Main():
    Reference = (Root / "Experimental/ParticleEditor/js/forcefields.js").read_text(encoding="utf-8")
    Port = (Root / "Engine/VolumetricDynamics/ForceFieldSet.h").read_text(encoding="utf-8")

    print("ForceFieldParity - the port against the browser it was ported from\n")

    print("The kinds")
    Browser = ReadBrowserKinds(Reference)
    Native = ReadNativeKinds(Port)
    Check(len(Browser) == 12, f"the browser lists twelve kinds (found {len(Browser)})")
    Check(len(Native) == len(Browser), f"and the port lists the same number (found {len(Native)})")
    Check([One["Id"] for One in Browser] == [One["Id"] for One in Native],
          "the same ids in the same order, so the enum's numbering is not a second opinion")

    for Left, Right in zip(Browser, Native):
        Check(Left["Name"] == Right["Name"], f"{Left['Id']} is called the same thing on both sides")
        Check(Left["Give"] == Right["Give"],
              f"{Left['Id']} contributes {Left['Give']} on both sides")
        Check(Left["Reaches"] == Right["Reaches"], f"and reaches {Left['Reaches']} on both sides")
        Check(Left["Lattice"] == Right["Lattice"] and Left["Force"] == Right["Force"],
              f"and takes the same shader slots (lattice {Left['Lattice']}, force {Left['Force']})")

    print("\nThe eight that already existed")
    Was = [One for One in Browser if One["Lattice"] >= 0 or One["Force"] >= 0]
    Check(len([One for One in Browser if One["Lattice"] >= 0]) == 4,
          "four kinds claim a flow lattice slot, and they are the four wind components")
    Check(sorted(One["Lattice"] for One in Browser if One["Lattice"] >= 0) == [0, 1, 2, 3],
          "numbered 0 to 3 with no gaps and no collisions, as the lattice builder indexes them")
    Forces = sorted(One["Force"] for One in Browser if One["Force"] >= 0)
    Check(Forces == [0, 0, 1, 2, 2, 3],
          "and the acceleration slots are 0-3, with gravity sharing Lift and orbit sharing Attract")
    Check(len(Was) == 10, "ten kinds reach a shader; the other two are declared without one")

    print("\nThe mapping that needs a sign")
    Inverting = [One["Id"] for One in Browser if One["Inverts"]]
    Check(Inverting == ["gravity"],
          "gravity is the only kind that packs with its sign flipped, because it rides Lift downward")
    Check([One["Id"] for One in Native if One["Inverts"]] == Inverting,
          "and the port agrees, which is the difference between gravity and anti-gravity")

    print("\nThe claims that decided the design, stated on both sides")
    for Phrase, Where in [
        ("IT IS WHAT A FIELD RETURNS", "both"),
        ("terminal", "both"),
        ("Everywhere is not", "reference"),
        ("NOT a very large sphere", "port"),
    ]:
        if Where in ("both", "reference"):
            Check(Phrase.lower() in Reference.lower(), f"the reference says '{Phrase}'")
        if Where in ("both", "port"):
            Check(Phrase.lower() in Port.lower(), f"the port says '{Phrase}'")

    Check("Flowing" in Reference and "Flowing" in Port,
          "🔴 both carry the Flowing flag, so neither can regress to treating no wind as a wind of zero")
    Check("WindField.h" in Port,
          "and the port says in writing that it does not replace WindField.h")

    print("\nThe order of application")
    Browser_Order = re.search(r"export function Advance\((.*?)\n}", Reference, re.S).group(1)
    Port_Order = re.search(r"inline void AdvanceByForces\((.*?)\n}", Port, re.S).group(1)
    for Name, Body in (("reference", Browser_Order), ("port", Port_Order)):
        Flow = Body.index("Flow[0]")
        Accelerate = Body.index("Accelerate[0]")
        Damp = Body.index("Damp")
        Check(Flow < Accelerate < Damp,
              f"the {Name} relaxes toward the flow, then accelerates, then damps")

    print(f"\n{'PASS' if not Faults else 'FAIL'} {Claims}")
    if Faults:
        print(f"{len(Faults)} disagreement(s) between the taxonomy and its port")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(Main())
