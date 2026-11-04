#=====================================================================================================================================
#                                                            CMAKELISTS.TXT
#=====================================================================================================================================
# 🧩 FrontierGeometryProcessing — B-rep geometry processing library (topology, geometry, Booleans, blends, sketch, feature tree, exchange)

# 📝 Production library shared by every application. Validation and evidence live in Exhibits/GeometryProcessing/Code.
#    Headers are included from the repository root: #include "Engine/GeometryProcessing/Topology/ModelPartition.h".

cmake_minimum_required(VERSION 3.20)

get_filename_component(FRONTIER_GEOMETRY_PROCESSING_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

#-------------------------------------------------------------------------------------------------------------------
#                                                      LIBRARY
#-------------------------------------------------------------------------------------------------------------------

add_library(FrontierGeometryProcessing STATIC
    Precision/ExactPredicates.cpp
    Diagnostics/OperationTrace.cpp
    Geometry/CurveGeometry.cpp
    Geometry/SurfaceGeometry.cpp
    Geometry/RollingBallCarrier.cpp
    Geometry/NurbsCarrier.cpp
    Geometry/ParallelCarrier.cpp
    Geometry/ProjectedCurve.cpp
    Containment/FaceDomain.cpp
    Containment/WindingNumber.cpp
    Tessellation/ChartTriangulation.cpp
    Tessellation/BodyTessellation.cpp
    Boolean/GeneralFuse.cpp
    Boolean/BooleanCatalogue.cpp
    Intersection/BernsteinPolynomial.cpp
    Intersection/ImplicitSurface.cpp
    Intersection/CurveSurfaceIntersection.cpp
    Intersection/IsoParameterCurve.cpp
    Intersection/SurfaceIntersection.cpp
    Intersection/IntersectionCurve.cpp
    Topology/ModelPartition.cpp
    Topology/EulerEdits.cpp
    Construction/PrimitiveConstruction.cpp
    Construction/BodyAssembly.cpp
    Construction/BodyModifiers.cpp
    Construction/BodyTransform.cpp
    Blend/RollingBallGeometry.cpp
    Blend/FairPatch.cpp
    Blend/EdgeBlend.cpp
    Blend/BlendCatalogue.cpp
    Local/FaceChange.cpp
    Local/FaceOperations.cpp
    Local/TopologyRebuild.cpp
    Local/RedundantTopology.cpp
    Local/FaceOperationCatalogue.cpp
    Sketch/SketchSolver.cpp
    Sketch/SketchProfile.cpp
    Sketch/SketchSection.cpp
    Sketch/SketchSpline.cpp
    Sketch/SketchCatalogue.cpp
    Sketch/SketchCorner.cpp
    Sketch/SketchRegion.cpp
    Sketch/SketchTrim.cpp
    Sketch/SketchFlow.cpp
    Sketch/SketchEditing.cpp
    Sketch/SketchConstraining.cpp
    Feature/ProfileSweep.cpp
    Feature/BodyCut.cpp
    Feature/PathSweep.cpp
    Feature/ReplayDigest.cpp
    Feature/TopologicalNaming.cpp
    Feature/FeatureTree.cpp
    Feature/FeatureCatalogue.cpp
    Surfacing/LinearSystem.cpp
    Surfacing/SurfaceDistanceCurve.cpp
    Surfacing/CurveFitting.cpp
    Surfacing/SectionLoft.cpp
    Surfacing/SurfaceAlgebra.cpp
    Surfacing/CurveNetwork.cpp
    Surfacing/PanelAssembly.cpp
    Surfacing/CarrierApproximation.cpp
    Inspection/BlendEncoding.cpp
    Audit/EnclosedVolume.cpp
    Assembly/AssemblyAnalysis.cpp
    Assembly/JointSolver.cpp
    Measurement/MassProperties.cpp
    Measurement/MinimumDistance.cpp
    Measurement/PlaneSection.cpp
    Drawing/DrawingProjection.cpp
    Drawing/DrawingSheet.cpp
    Drawing/DrawingExport.cpp
    Exchange/StepExchange.cpp
    Exchange/StepReading.cpp
    Exchange/StlExchange.cpp
    Audit/ModelAudit.cpp
    Audit/IntersectionAudit.cpp
    Audit/TessellationAudit.cpp
    Inspection/SpecimenCatalogue.cpp
    Inspection/InspectionEncoding.cpp
    Inspection/BooleanEncoding.cpp
    Inspection/FaceOperationEncoding.cpp
    Inspection/FeatureEncoding.cpp
    Inspection/IntersectionCatalogue.cpp
    Inspection/IntersectionEncoding.cpp
    Inspection/TessellationEncoding.cpp
)
target_include_directories(FrontierGeometryProcessing PUBLIC "${FRONTIER_GEOMETRY_PROCESSING_ROOT}")
target_compile_features(FrontierGeometryProcessing PUBLIC cxx_std_20)

# 📝 Development trace (Diagnostics/OperationTrace.h): ▶ / ◀ lines per operation with durations, flushed per line so a
#    freeze names its operation. ON while SolidArc is in development; a release turns it OFF and every macro vanishes.
option(FRONTIER_DEVELOPMENT_TRACE "Write the operation trace (FRONTIER_TRACE_SCOPE / FRONTIER_TRACE)" ON)
if(FRONTIER_DEVELOPMENT_TRACE)
    target_compile_definitions(FrontierGeometryProcessing PUBLIC FRONTIER_DEVELOPMENT_TRACE)
endif()

# 📝 Identifiers use Greek / mathematical Unicode (SKILL-formatting § 4): MSVC needs /utf-8, also in every consumer.
if(MSVC)
    target_compile_options(FrontierGeometryProcessing PUBLIC /utf-8)
    target_compile_options(FrontierGeometryProcessing PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(FrontierGeometryProcessing PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()
