import patternGeometrySource from "./patternGeometry.js?raw";
import geometricModuleSource from "./geometricConstructions.js?raw";
import weaveModuleSource from "./patternWeaves.js?raw";
import ornamentDrawingSource from "./ornamentDrawing.js?raw";
import ornamentalSource from "./ornamentalConstructions.js?raw";
import referenceModuleSource from "./referencePatterns.js?raw";
import rugDesignModuleSource from "./rugDesigns.js?raw";
import rugCompositionModuleSource from "./rugCompositions.js?raw";
import stitchModuleSource from "./patternStitches.js?raw";
import textileModuleSource from "./patternLibrary.js?raw";
import collectionModuleSource from "./patternCollections.js?raw";
import patternImportSource from "./patternImport.js?raw";
import patternDocumentSource from "./patternDocument.js?raw";
import leatherSourceModule from "./leatherSource.js?raw";
import patternRuntimeSource from "./patternRuntime.js?raw";
import recipeModuleSource from "./materialProfiles.js?raw";
import materialModuleSource from "./materials.js?raw";
import kernelModuleSource from "./surfaceKernels.js?raw";
import botanicalModuleSource from "./botanicalKernels.js?raw";
import architectureModuleSource from "./architecturalKernels.js?raw";
import sandModuleSource from "./sandKernels.js?raw";
const shaderSource =
  geometricModuleSource +
  "\n" +
  weaveModuleSource +
  "\n" +
  ornamentDrawingSource +
  "\n" +
  referenceModuleSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  ornamentalSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  stitchModuleSource +
  "\n" +
  rugDesignModuleSource +
  "\n" +
  rugCompositionModuleSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  textileModuleSource
    .replace(/^import[\s\S]*?;\s*/gm, "")
    .replace(/^export \{[^}]*\} from [^;]*;\s*/gm, "") +
  "\n" +
  collectionModuleSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  patternImportSource +
  "\n" +
  patternGeometrySource +
  "\n" +
  patternDocumentSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  leatherSourceModule +
  "\n" +
  patternRuntimeSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  recipeModuleSource +
  "\n" +
  kernelModuleSource +
  "\n" +
  botanicalModuleSource +
  "\n" +
  architectureModuleSource +
  "\n" +
  sandModuleSource +
  "\n" +
  materialModuleSource.slice(
    materialModuleSource.indexOf("export function normalizeMaterial("),
    materialModuleSource.indexOf("export function createBallGeometry("),
  );

export function exportMaterialJSON(params) {
  return JSON.stringify(
    { schema: "alloy.material.v6", version: 6, material: params },
    null,
    2,
  );
}
export function exportMaterialShader(params) {
  return `import * as THREE from 'three';\n\nexport const preset = ${JSON.stringify(params, null, 2)};\n\n${shaderSource}\n\nexport default createMaterial(preset);\n`;
}
