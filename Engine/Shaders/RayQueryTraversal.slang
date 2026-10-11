//============================================================================================================================================
//                                                        RAYQUERYTRAVERSAL.SLANG
//============================================================================================================================================
// 📦 Hardware closest-hit and occlusion queries preserving the software traversal's instance and primitive identities.

layout(set = 1, binding = 0) uniform accelerationStructureEXT TriangleAcceleration;

TraversalResult TraceTraversal(vec3 Origin, vec3 Direction, vec3 ReciprocalDirection, float MaximumDistance)
{
    TraversalResult Intersection;
    Intersection.t = kInf;
    Intersection.primitive = Intersection.instance = 0xFFFFFFFFu;
    Intersection.valid = false;
    rayQueryEXT Query;
    // Opaque traversal returns the geometric hit. The shared caller applies material alpha rejection and retraces.
    rayQueryInitializeEXT(Query, TriangleAcceleration, gl_RayFlagsOpaqueEXT, 0xFFu, Origin, 0.0, Direction, MaximumDistance);
    while (rayQueryProceedEXT(Query)) { }
    if (rayQueryGetIntersectionTypeEXT(Query, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
    {
        Intersection.t = rayQueryGetIntersectionTEXT(Query, true);
        Intersection.primitive = rayQueryGetIntersectionPrimitiveIndexEXT(Query, true);
        Intersection.instance = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(Query, true));
        Intersection.valid = true;
    }
    return Intersection;
}

bool TraceTraversalOccluded(vec3 Origin, vec3 Direction, vec3 ReciprocalDirection, float MaximumDistance)
{
    rayQueryEXT Query;
    rayQueryInitializeEXT(Query, TriangleAcceleration, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          0xFFu, Origin, 0.0, Direction, MaximumDistance);
    while (rayQueryProceedEXT(Query)) { }
    return rayQueryGetIntersectionTypeEXT(Query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}
