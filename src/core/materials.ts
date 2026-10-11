import * as THREE from 'three';

export interface TextureLayer {
  id: string;
  name: string;
  type: 'base_asphalt' | 'aggregate_wear' | 'tire_skidmarks' | 'wetness_puddles' | 'cracks_repairs' | 'paving_bond' | 'markings';
  enabled: boolean;
  opacity: number;
  blendMode: 'normal' | 'multiply' | 'overlay' | 'screen';
  roughness: number;
  metallic: number;
  uvScale: number;
  color?: string;
  pavingPattern?: 'herringbone45' | 'herringbone90' | 'running_bond' | 'cobblestone' | 'granite_slabs' | 'hexagonal' | 'dutch_clay' | 'tactile_blister';
}

export class MaterialManager {
  private canvasCache: Map<string, HTMLCanvasElement> = new Map();
  private textureCache: Map<string, THREE.CanvasTexture> = new Map();

  // Layer stack configuration
  public layers: TextureLayer[] = [
    {
      id: 'base_asphalt',
      name: 'Base SMA Asphalt',
      type: 'base_asphalt',
      enabled: true,
      opacity: 1.0,
      blendMode: 'normal',
      roughness: 0.82,
      metallic: 0.05,
      uvScale: 8.0,
      color: '#242426'
    },
    {
      id: 'aggregate_wear',
      name: 'Aggregate & Microtexture',
      type: 'aggregate_wear',
      enabled: true,
      opacity: 0.65,
      blendMode: 'overlay',
      roughness: 0.75,
      metallic: 0.0,
      uvScale: 16.0
    },
    {
      id: 'tire_skidmarks',
      name: 'Tire Wheelpaths & Skidmarks',
      type: 'tire_skidmarks',
      enabled: true,
      opacity: 0.55,
      blendMode: 'multiply',
      roughness: 0.65,
      metallic: 0.1,
      uvScale: 1.0
    },
    {
      id: 'wetness_puddles',
      name: 'Moisture & Surface Puddles',
      type: 'wetness_puddles',
      enabled: false,
      opacity: 0.8,
      blendMode: 'screen',
      roughness: 0.15,
      metallic: 0.2,
      uvScale: 3.0
    },
    {
      id: 'cracks_repairs',
      name: 'Cracks & Bitumen Tar Snakes',
      type: 'cracks_repairs',
      enabled: true,
      opacity: 0.75,
      blendMode: 'multiply',
      roughness: 0.45,
      metallic: 0.15,
      uvScale: 4.0
    },
    {
      id: 'paving_bond',
      name: 'Sidewalk Paving Pattern',
      type: 'paving_bond',
      enabled: true,
      opacity: 1.0,
      blendMode: 'normal',
      roughness: 0.72,
      metallic: 0.02,
      uvScale: 4.0,
      pavingPattern: 'granite_slabs'
    }
  ];

  // Cached materials
  public roadMaterial!: THREE.MeshStandardMaterial;
  public curbMaterial!: THREE.MeshStandardMaterial;
  public gutterMaterial!: THREE.MeshStandardMaterial;
  public sidewalkMaterial!: THREE.MeshStandardMaterial;
  public markingMaterial!: THREE.MeshStandardMaterial;
  public yellowMarkingMaterial!: THREE.MeshStandardMaterial;
  public bridgeSteelMaterial!: THREE.MeshStandardMaterial;
  public bridgeConcreteMaterial!: THREE.MeshStandardMaterial;
  public railingMaterial!: THREE.MeshStandardMaterial;
  public barrierMaterial!: THREE.MeshStandardMaterial;
  public wireframeMaterial!: THREE.MeshBasicMaterial;
  public tactileMaterial!: THREE.MeshStandardMaterial;
  public railwayBallastMaterial!: THREE.MeshStandardMaterial;
  public railwaySleeperMaterial!: THREE.MeshStandardMaterial;
  public railwayRailMaterial!: THREE.MeshStandardMaterial;
  public tunnelWallMaterial!: THREE.MeshStandardMaterial;

  constructor() {
    this.initMaterials();
  }

  private initMaterials() {
    // 1. Generate Road Textures (composite canvas)
    const roadAlbedoTex = this.generateCompositeRoadTexture();
    const roadNormalTex = this.generateNormalTexture('road_normal', 1024, 1024, 0.4);
    const roadRoughnessTex = this.generateRoughnessTexture('road_rough', 1024, 1024, 0.78);

    this.roadMaterial = new THREE.MeshStandardMaterial({
      map: roadAlbedoTex,
      normalMap: roadNormalTex,
      normalScale: new THREE.Vector2(0.6, 0.6),
      roughnessMap: roadRoughnessTex,
      roughness: 0.82,
      metalness: 0.05,
      envMapIntensity: 0.8
    });

    // 2. Sidewalk Paving Material
    const sidewalkTex = this.generatePavingTexture('granite_slabs');
    const sidewalkNormalTex = this.generatePavingNormal('granite_slabs');
    this.sidewalkMaterial = new THREE.MeshStandardMaterial({
      map: sidewalkTex,
      normalMap: sidewalkNormalTex,
      normalScale: new THREE.Vector2(0.5, 0.5),
      roughness: 0.75,
      metalness: 0.02
    });

    // 3. Curb Material (Dressed Granite / Concrete Chamfer)
    const curbTex = this.generateConcreteTexture('#8a8d91', 0.2);
    this.curbMaterial = new THREE.MeshStandardMaterial({
      map: curbTex,
      roughness: 0.70,
      metalness: 0.05
    });

    // 4. Gutter Channel
    this.gutterMaterial = new THREE.MeshStandardMaterial({
      map: this.generateConcreteTexture('#45474a', 0.25),
      roughness: 0.65,
      metalness: 0.05
    });

    // 5. White Thermoplastic Marking Material (Reflective glass beads)
    this.markingMaterial = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      roughness: 0.45,
      metalness: 0.1,
      polygonOffset: true,
      polygonOffsetFactor: -2,
      polygonOffsetUnits: -2
    });

    // 6. Yellow Marking Material
    this.yellowMarkingMaterial = new THREE.MeshStandardMaterial({
      color: 0xf5b82e,
      roughness: 0.45,
      metalness: 0.1,
      polygonOffset: true,
      polygonOffsetFactor: -2,
      polygonOffsetUnits: -2
    });

    // 7. Bridge Materials
    this.bridgeSteelMaterial = new THREE.MeshStandardMaterial({
      color: 0x3b444b, // weathered structural steel
      roughness: 0.35,
      metalness: 0.85
    });

    this.bridgeConcreteMaterial = new THREE.MeshStandardMaterial({
      color: 0x828489,
      roughness: 0.78,
      metalness: 0.05
    });

    // 8. Railings & Barriers
    this.railingMaterial = new THREE.MeshStandardMaterial({
      color: 0x222629,
      roughness: 0.3,
      metalness: 0.8
    });

    this.barrierMaterial = new THREE.MeshStandardMaterial({
      color: 0x7a7e85,
      roughness: 0.85,
      metalness: 0.02
    });

    // 9. Wireframe Material for pristine quad topology inspection
    this.wireframeMaterial = new THREE.MeshBasicMaterial({
      color: 0x00e5ff,
      wireframe: true,
      wireframeLinewidth: 1.5
    });

    // 10. Yellow ADA Tactile Blister Paving
    this.tactileMaterial = new THREE.MeshStandardMaterial({
      color: 0xe6b800,
      roughness: 0.5,
      metalness: 0.05,
      map: this.generateTactileTexture()
    });

    // 11. Railway Materials
    this.railwayBallastMaterial = new THREE.MeshStandardMaterial({
      color: 0x544e4b,
      roughness: 0.95,
      metalness: 0.05,
      normalMap: this.generateBallastNormal(),
      normalScale: new THREE.Vector2(1.0, 1.0)
    });

    this.railwaySleeperMaterial = new THREE.MeshStandardMaterial({
      color: 0x6e6c69, // concrete tie
      roughness: 0.8,
      metalness: 0.1
    });

    this.railwayRailMaterial = new THREE.MeshStandardMaterial({
      color: 0xb5bcc7, // polished steel running rail
      roughness: 0.18,
      metalness: 0.95
    });

    // 12. Tunnel Materials
    this.tunnelWallMaterial = new THREE.MeshStandardMaterial({
      color: 0x3d4147,
      roughness: 0.65,
      metalness: 0.1
    });
  }

  /**
   * Procedural road composite texture combining all enabled layers in layerstack
   */
  public generateCompositeRoadTexture(): THREE.CanvasTexture {
    const size = 1024;
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d')!;

    // 1. Base Asphalt
    ctx.fillStyle = '#222326';
    ctx.fillRect(0, 0, size, size);

    // Fine asphalt grain noise
    const imgData = ctx.getImageData(0, 0, size, size);
    const data = imgData.data;
    for (let i = 0; i < data.length; i += 4) {
      const noise = (Math.random() - 0.5) * 36;
      data[i] = Math.min(255, Math.max(0, data[i] + noise));
      data[i + 1] = Math.min(255, Math.max(0, data[i + 1] + noise));
      data[i + 2] = Math.min(255, Math.max(0, data[i + 2] + noise));
    }
    ctx.putImageData(imgData, 0, 0);

    // 2. Aggregate Specks (bitumen crushed rock)
    const aggLayer = this.layers.find(l => l.id === 'aggregate_wear');
    if (aggLayer && aggLayer.enabled) {
      ctx.fillStyle = `rgba(180, 180, 185, ${0.15 * aggLayer.opacity})`;
      for (let i = 0; i < 4000; i++) {
        const x = Math.random() * size;
        const y = Math.random() * size;
        const r = Math.random() * 2.2 + 0.5;
        ctx.beginPath();
        ctx.arc(x, y, r, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    // 3. Wheelpath wear bands (darker polished bitumen lanes)
    const skidLayer = this.layers.find(l => l.id === 'tire_skidmarks');
    if (skidLayer && skidLayer.enabled) {
      ctx.fillStyle = `rgba(15, 15, 17, ${0.4 * skidLayer.opacity})`;
      // Left wheelpath
      ctx.fillRect(size * 0.18, 0, size * 0.16, size);
      // Right wheelpath
      ctx.fillRect(size * 0.66, 0, size * 0.16, size);
    }

    // 4. Cracks & Bitumen Tar Snakes
    const crackLayer = this.layers.find(l => l.id === 'cracks_repairs');
    if (crackLayer && crackLayer.enabled) {
      ctx.strokeStyle = `rgba(10, 10, 12, ${0.75 * crackLayer.opacity})`;
      ctx.lineWidth = 3;
      ctx.lineCap = 'round';
      ctx.lineJoin = 'round';
      // Draw tar snakes
      for (let c = 0; c < 5; c++) {
        ctx.beginPath();
        let cx = Math.random() * size;
        let cy = Math.random() * size;
        ctx.moveTo(cx, cy);
        for (let s = 0; s < 12; s++) {
          cx += (Math.random() - 0.5) * 80;
          cy += (Math.random() - 0.5) * 80;
          ctx.lineTo(cx, cy);
        }
        ctx.stroke();
      }
    }

    // 5. Wetness & Puddles
    const wetLayer = this.layers.find(l => l.id === 'wetness_puddles');
    if (wetLayer && wetLayer.enabled) {
      ctx.fillStyle = `rgba(8, 10, 14, ${0.5 * wetLayer.opacity})`;
      for (let p = 0; p < 4; p++) {
        ctx.beginPath();
        const px = Math.random() * size;
        const py = Math.random() * size;
        const rx = 40 + Math.random() * 70;
        const ry = 25 + Math.random() * 50;
        ctx.ellipse(px, py, rx, ry, Math.random() * Math.PI, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    const texture = new THREE.CanvasTexture(canvas);
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.RepeatWrapping;
    texture.repeat.set(2, 6);
    return texture;
  }

  public updateRoadTexture() {
    const newTex = this.generateCompositeRoadTexture();
    this.roadMaterial.map = newTex;
    this.roadMaterial.needsUpdate = true;
  }

  /**
   * Procedural Normal Map for surface microrelief
   */
  public generateNormalTexture(key: string, width: number, height: number, strength: number = 0.5): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d')!;
    const imgData = ctx.createImageData(width, height);
    const data = imgData.data;

    for (let i = 0; i < data.length; i += 4) {
      // Perturb standard normal [128, 128, 255]
      const nx = 128 + (Math.random() - 0.5) * 60 * strength;
      const ny = 128 + (Math.random() - 0.5) * 60 * strength;
      const nz = 255;
      data[i] = Math.min(255, Math.max(0, nx));
      data[i + 1] = Math.min(255, Math.max(0, ny));
      data[i + 2] = nz;
      data[i + 3] = 255;
    }
    ctx.putImageData(imgData, 0, 0);

    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(4, 12);
    return tex;
  }

  public generateRoughnessTexture(key: string, width: number, height: number, baseRough: number = 0.8): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d')!;
    const imgData = ctx.createImageData(width, height);
    const data = imgData.data;

    const baseVal = Math.floor(baseRough * 255);
    for (let i = 0; i < data.length; i += 4) {
      const val = Math.min(255, Math.max(0, baseVal + (Math.random() - 0.5) * 40));
      data[i] = val;
      data[i + 1] = val;
      data[i + 2] = val;
      data[i + 3] = 255;
    }
    ctx.putImageData(imgData, 0, 0);

    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(4, 12);
    return tex;
  }

  /**
   * Procedural Sidewalk Paving Patterns
   */
  public generatePavingTexture(pattern: TextureLayer['pavingPattern'] = 'granite_slabs'): THREE.CanvasTexture {
    const size = 1024;
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d')!;

    // Base tone
    ctx.fillStyle = '#9b9da0';
    ctx.fillRect(0, 0, size, size);

    if (pattern === 'granite_slabs') {
      // 600x600 modern concrete/granite slabs with chamfered joints
      const tileSize = 128;
      ctx.lineWidth = 3;
      ctx.strokeStyle = '#4a4d52';

      for (let y = 0; y < size; y += tileSize) {
        for (let x = 0; x < size; x += tileSize) {
          // Slight tonal variation per slab
          const tone = 150 + Math.floor(Math.random() * 25);
          ctx.fillStyle = `rgb(${tone}, ${tone + 1}, ${tone + 2})`;
          ctx.fillRect(x + 2, y + 2, tileSize - 4, tileSize - 4);
          ctx.strokeRect(x, y, tileSize, tileSize);
        }
      }
    } else if (pattern === 'herringbone45' || pattern === 'herringbone90') {
      // Interlocking brick pavers
      const brickW = 64;
      const brickH = 32;
      ctx.strokeStyle = '#3e4147';
      ctx.lineWidth = 2;

      for (let y = 0; y < size; y += brickH) {
        const offset = (Math.floor(y / brickH) % 2) * (brickW / 2);
        for (let x = -brickW; x < size + brickW; x += brickW) {
          const toneR = 175 + Math.floor(Math.random() * 25);
          const toneG = 160 + Math.floor(Math.random() * 20);
          const toneB = 150 + Math.floor(Math.random() * 20);
          ctx.fillStyle = `rgb(${toneR}, ${toneG}, ${toneB})`;
          ctx.fillRect(x + offset + 1, y + 1, brickW - 2, brickH - 2);
          ctx.strokeRect(x + offset, y, brickW, brickH);
        }
      }
    } else if (pattern === 'cobblestone') {
      // Historic fan/arch cobblestones
      const cobbleSize = 40;
      ctx.strokeStyle = '#333538';
      ctx.lineWidth = 2.5;

      for (let y = 0; y < size; y += cobbleSize) {
        const rowOffset = (Math.sin(y * 0.05) * 15);
        for (let x = 0; x < size; x += cobbleSize) {
          const tone = 110 + Math.floor(Math.random() * 35);
          ctx.fillStyle = `rgb(${tone}, ${tone - 5}, ${tone - 10})`;
          const cx = x + rowOffset + (Math.random() - 0.5) * 6;
          const cy = y + (Math.random() - 0.5) * 6;
          ctx.beginPath();
          ctx.roundRect(cx + 2, cy + 2, cobbleSize - 4, cobbleSize - 4, 8);
          ctx.fill();
          ctx.stroke();
        }
      }
    } else if (pattern === 'hexagonal') {
      // Hexagonal paving tiles
      const r = 40;
      const h = r * Math.sqrt(3);
      ctx.strokeStyle = '#3a3c40';
      ctx.lineWidth = 2;

      for (let y = 0; y < size + h; y += h) {
        const row = Math.floor(y / h);
        const xOffset = (row % 2) * (1.5 * r);
        for (let x = -r * 2; x < size + r * 2; x += 3 * r) {
          const tone = 160 + Math.floor(Math.random() * 20);
          ctx.fillStyle = `rgb(${tone}, ${tone}, ${tone})`;
          this.drawHexagon(ctx, x + xOffset, y, r - 2);
          ctx.fill();
          this.drawHexagon(ctx, x + xOffset, y, r);
          ctx.stroke();
        }
      }
    } else {
      // Running bond
      const bw = 96;
      const bh = 48;
      ctx.strokeStyle = '#444';
      ctx.lineWidth = 2;
      for (let y = 0; y < size; y += bh) {
        const row = Math.floor(y / bh);
        const shift = (row % 2) * (bw / 2);
        for (let x = -bw; x < size + bw; x += bw) {
          const t = 150 + Math.floor(Math.random() * 25);
          ctx.fillStyle = `rgb(${t}, ${t}, ${t})`;
          ctx.fillRect(x + shift + 1, y + 1, bw - 2, bh - 2);
          ctx.strokeRect(x + shift, y, bw, bh);
        }
      }
    }

    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(4, 4);
    return tex;
  }

  private drawHexagon(ctx: CanvasRenderingContext2D, x: number, y: number, r: number) {
    ctx.beginPath();
    for (let i = 0; i < 6; i++) {
      const angle = (Math.PI / 3) * i;
      const px = x + r * Math.cos(angle);
      const py = y + r * Math.sin(angle);
      if (i === 0) ctx.moveTo(px, py);
      else ctx.lineTo(px, py);
    }
    ctx.closePath();
  }

  public generatePavingNormal(pattern: TextureLayer['pavingPattern'] = 'granite_slabs'): THREE.CanvasTexture {
    return this.generateNormalTexture('paving_norm', 1024, 1024, 0.7);
  }

  public setPavingPattern(pattern: TextureLayer['pavingPattern']) {
    const layer = this.layers.find(l => l.id === 'paving_bond');
    if (layer) {
      layer.pavingPattern = pattern;
      this.sidewalkMaterial.map = this.generatePavingTexture(pattern);
      this.sidewalkMaterial.needsUpdate = true;
    }
  }

  /**
   * Concrete stippled texture
   */
  private generateConcreteTexture(hexColor: string, noiseStrength: number): THREE.CanvasTexture {
    const size = 512;
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d')!;

    ctx.fillStyle = hexColor;
    ctx.fillRect(0, 0, size, size);

    const imgData = ctx.getImageData(0, 0, size, size);
    const data = imgData.data;
    for (let i = 0; i < data.length; i += 4) {
      const n = (Math.random() - 0.5) * 255 * noiseStrength;
      data[i] = Math.min(255, Math.max(0, data[i] + n));
      data[i + 1] = Math.min(255, Math.max(0, data[i + 1] + n));
      data[i + 2] = Math.min(255, Math.max(0, data[i + 2] + n));
    }
    ctx.putImageData(imgData, 0, 0);

    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(4, 4);
    return tex;
  }

  /**
   * Yellow ADA Warning Blister Tactile Texture
   */
  private generateTactileTexture(): THREE.CanvasTexture {
    const size = 512;
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d')!;

    ctx.fillStyle = '#e6b800'; // Safety yellow
    ctx.fillRect(0, 0, size, size);

    // Truncated dome blister pattern
    const spacing = 32;
    for (let y = spacing / 2; y < size; y += spacing) {
      for (let x = spacing / 2; x < size; x += spacing) {
        // Shadow base
        ctx.fillStyle = '#c49a00';
        ctx.beginPath();
        ctx.arc(x, y, 10, 0, Math.PI * 2);
        ctx.fill();

        // Highlight dome top
        ctx.fillStyle = '#ffd633';
        ctx.beginPath();
        ctx.arc(x - 2, y - 2, 7, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(6, 6);
    return tex;
  }

  /**
   * Ballast Stone Normal
   */
  private generateBallastNormal(): THREE.CanvasTexture {
    return this.generateNormalTexture('ballast_norm', 512, 512, 0.9);
  }
}
