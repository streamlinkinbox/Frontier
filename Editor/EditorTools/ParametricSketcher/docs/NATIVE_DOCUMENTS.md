# Native `.arc` documents — version 1

SolidArc's native part-document format is a **versioned construction journal**, not a triangulated export. It retains
creation operations, their parameters, associative recipes, live dimensions, constraints, names, colours, workplanes,
selection and direct edits by saving the successful top-level console commands that created them. Reopening the file
replays that journal through the same NURBS/B-rep kernel.

That choice is deliberate: a native document must preserve editable parametric intent. A mesh archive would preserve
appearance but lose the relationship between a sketch and its extrude, or the ability to update a driving dimension.
Neutral exchange (STEP/DXF/STL) is a separately planned capability and must not silently replace the native source.

## Commands

```text
save bracket.arc        # create or replace `bracket.arc`
save bracket            # `.arc` is appended when the path has no extension
save                    # save again to the document currently open
open bracket.arc        # replace the current scene only if all document commands validate
```

`save` requires the `.arc` extension when one is supplied. On a replacement save, the previous good file is copied to
`bracket.arc.bak`, the new bytes are written to `bracket.arc.tmp`, flushed, then renamed into place. If writing fails,
the original document remains intact. Temporary files are removed on an observed write or rename failure.

An `.arc` document begins with the exact version marker:

```text
# SolidArc native document v1
# Construction journal. Edit only with the documented .arc grammar; save keeps a .bak recovery copy.

workplane xy --origin=(3,4,0)
rect (0,0) (20,12) --name="Plate Profile"
extrude "Plate Profile" 5 --name="Plate Body"
```

Names containing spaces are quoted when saved. The format is ordinary UTF-8 text with one canonical command per line,
so it is diff-friendly and can be reviewed in source control.

## Safe opening semantics

`open` first checks the version marker, then replays the document in a fresh, isolated `ConsoleHost`. Only if every
command succeeds is that host's scene, undo history, dimensions, constraints, workplanes, hotkeys, view state, and
journal adopted by the live host. A malformed command, a geometric refusal, a missing referenced figure, or an
incompatible header **leaves the open document unchanged**.

Existing `Scripts/*.arc` proof scripts deliberately do not have the native-document header. Continue to run those with
`SolidArc Scripts/Phase…arc`; `open` refuses them instead of accidentally treating a test/proof script as a part file.
Likewise, `save`, `open`, proof `render` commands, help, and other read-only diagnostics are not emitted into the
construction journal.

A modal drawing operation is never restored half-complete: a saved model contains only geometry that was committed by
its completed input sequence. A reopening begins without an active modal tool.

## Coverage and current boundary

The v1 journal covers all successful top-level C++ console operations, including 2D construction/booleans/offsets,
3D B-rep operations, sketches, associative loft/sweep/extrude recipes, direct `move` edits, constraints, dimensions,
workplanes, selection and display configuration. Replaying operations through the kernel preserves exact NURBS/B-rep
rather than serialising display triangles.

This is native persistence, **not interchange**. It does not make a `.arc` readable by other CAD systems and does not
replace planned STEP, IGES, DXF, STL, 3MF, or OBJ import/export. The file version is intentionally explicit so future
schema migrations can be made without guessing at older data.

## Verification

`Verification/DocumentVerification.cpp` performs a mixed 2D/3D round trip with quoted names, a named workplane, a
live extrude recipe, a direct B-rep edit, and a 2D constraint. It asserts:

1. `.arc` extension normalisation and the exact v1 header;
2. same geometric fingerprint and B-rep volume after open;
3. associative regeneration still works after reload;
4. the second save creates a `.bak` recovery file; and
5. an invalid `.arc` is refused transactionally without changing the current model.
