import * as THREE from 'three';
import { GLTFExporter } from 'three/addons/exporters/GLTFExporter.js';
import { OBJExporter } from 'three/addons/exporters/OBJExporter.js';

export class SceneExporter {
  /**
   * Export scene or group to GLB format
   */
  public static exportGLB(root: THREE.Object3D, filename: string = 'RoadNetwork.glb') {
    const exporter = new GLTFExporter();
    exporter.parse(
      root,
      (gltf) => {
        const blob = new Blob([gltf as ArrayBuffer], { type: 'application/octet-stream' });
        this.downloadBlob(blob, filename);
      },
      (error) => {
        console.error('Error exporting GLB:', error);
      },
      { binary: true }
    );
  }

  /**
   * Export scene or group to Wavefront OBJ
   */
  public static exportOBJ(root: THREE.Object3D, filename: string = 'RoadNetwork.obj') {
    const exporter = new OBJExporter();
    const result = exporter.parse(root);
    const blob = new Blob([result], { type: 'text/plain' });
    this.downloadBlob(blob, filename);
  }

  /**
   * Export project layout as JSON
   */
  public static exportJSON(data: any, filename: string = 'road-project.json') {
    const jsonStr = JSON.stringify(data, null, 2);
    const blob = new Blob([jsonStr], { type: 'application/json' });
    this.downloadBlob(blob, filename);
  }

  private static downloadBlob(blob: Blob, filename: string) {
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(link.href);
  }
}
