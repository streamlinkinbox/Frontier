import * as H from "../Source/GeometryHierarchy.js";
import { ConstructSphereFacets } from "./GeometryHierarchyCorpus.mjs";
const Source = ConstructSphereFacets(4, -1);
const Hierarchy = H.BuildGeometryHierarchy(Source, { GroupLimit: 8 });
const V = H.ValidateGeometryHierarchy(Source, Hierarchy, {});
console.log("Levels:", Hierarchy.Levels.length, "Clusters/level:", Hierarchy.Levels.map(L => L.Clusters.length).join(","));
console.log("Tris/level:", Hierarchy.Levels.map(L => L.TriangleCount).join(","));
console.log("MaxErr/level:", Hierarchy.Levels.map(L => Math.max(...L.Clusters.map(id => Hierarchy.Clusters[id].Error)).toFixed(4)).join(","));
console.log("Failures:", [...new Set(V.Failures)].slice(0, 12).join("\n"));
