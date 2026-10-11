//============================================================================================================================================
//                                                           GEOMETRYINTERCHANGE.H
//============================================================================================================================================
// 📦 Optional versioned C geometry extension. ABI3 projects without this export retain their rigid-only behaviour.

#pragma once
#include "ProjectInterchange.h"

// Calls are synchronous while the code image and ProjectRecord are alive. Neither side retains caller pointers.
// Positions are packed xyz, in the host's +Z-up metre world. Rest is immutable authored placement geometry;
// Current is the completed simulated snapshot, already rigidly posed (the host must not apply the wheel pose again).
// VertexCount == 0 negotiates a subject without accessing pointers; it works before project construction.
// Result: 0 unsupported/inactive; 1 accepted; 2 refused. Revision 0 means authored/rest, not live deformation.
typedef struct FrontierProjectGeometryReading
{
    uint32_t       StructureSize;
    uint32_t       InterchangeNumber;
    const char*    SubjectName;
    const float*   RestPositions;
    float*         CurrentPositions;
    uint32_t       VertexCount;
    uint64_t       Revision;
    const char*    MaterialName;
} FrontierProjectGeometryReading;

typedef uint32_t (FRONTIER_CODE_IMAGE_CALL* FrontierProjectGeometry)(void*, FrontierProjectGeometryReading*);
