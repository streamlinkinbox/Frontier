import * as THREE from 'three';
import { RoadSpline } from './spline';
import { MaterialManager } from './materials';

export interface RailwayConfig {
  id: string;
  name: string;
  gauge: number; // 1.435m standard gauge
  sleeperSpacing: number; // 0.60m
  ballastWidth: number; // 4.2m
  ballastHeight: number; // 0.35m
  isDoubleTrack?: boolean;
}

export class RailwayBuilder {
  /**
   * Builds railway track: ballast bed, concrete sleepers, and steel rails.
   */
  public static buildRailway(
    spline: RoadSpline,
    config: RailwayConfig,
    materials: MaterialManager
  ): THREE.Group {
    const group = new THREE.Group();
    group.name = `railway_${config.id}`;

    const length = spline.totalLength;
    const stepSize = 1.0;
    const numSlices = Math.max(3, Math.ceil(length / stepSize) + 1);

    const halfGauge = config.gauge * 0.5;
    const halfBallastTop = (config.ballastWidth * 0.5);
    const halfBallastBot = halfBallastTop + 0.6;
    const ballastH = config.ballastHeight;

    // 1. Ballast Embankment (Trapezoidal crushed stone profile)
    const bPos: number[] = [];
    const bNorm: number[] = [];
    const bUV: number[] = [];
    const bIndices: number[] = [];

    for (let s = 0; s < numSlices; s++) {
      const d = (s / (numSlices - 1)) * length;
      const sp = spline.getSampleAtDistance(d);
      const norm = sp.normal!;
      const binorm = sp.binormal!;

      const pts = [
        { x: -halfBallastBot, y: -ballastH },
        { x: -halfBallastTop, y: 0 },
        { x: halfBallastTop, y: 0 },
        { x: halfBallastBot, y: -ballastH }
      ];

      for (const p of pts) {
        const vx = sp.position.x + norm.x * p.x + binorm.x * p.y;
        const vy = sp.position.y + norm.y * p.x + binorm.y * p.y;
        const vz = sp.position.z + norm.z * p.x + binorm.z * p.y;
        bPos.push(vx, vy, vz);
        bNorm.push(0, 1, 0);
        bUV.push(p.x * 0.3, d * 0.3);
      }
    }

    for (let s = 0; s < numSlices - 1; s++) {
      const rowA = s * 4;
      const rowB = (s + 1) * 4;
      for (let j = 0; j < 3; j++) {
        bIndices.push(rowA + j, rowB + j, rowB + j + 1);
        bIndices.push(rowA + j, rowB + j + 1, rowA + j + 1);
      }
    }

    const ballastGeom = new THREE.BufferGeometry();
    ballastGeom.setAttribute('position', new THREE.Float32BufferAttribute(bPos, 3));
    ballastGeom.setAttribute('normal', new THREE.Float32BufferAttribute(bNorm, 3));
    ballastGeom.setAttribute('uv', new THREE.Float32BufferAttribute(bUV, 2));
    ballastGeom.setIndex(bIndices);
    const ballastMesh = new THREE.Mesh(ballastGeom, materials.railwayBallastMaterial);
    ballastMesh.receiveShadow = true;
    group.add(ballastMesh);

    // 2. Concrete Sleepers / Ties (Instanced Mesh for high performance)
    const sleeperSpacing = config.sleeperSpacing || 0.60;
    const numSleepers = Math.floor(length / sleeperSpacing);
    const sleeperGeom = new THREE.BoxGeometry(2.6, 0.18, 0.24);
    const sleeperInstanced = new THREE.InstancedMesh(sleeperGeom, materials.railwaySleeperMaterial, numSleepers);
    sleeperInstanced.castShadow = true;
    sleeperInstanced.receiveShadow = true;

    const dummy = new THREE.Object3D();
    for (let i = 0; i < numSleepers; i++) {
      const d = i * sleeperSpacing;
      const sp = spline.getSampleAtDistance(d);
      dummy.position.copy(sp.position).addScaledVector(sp.binormal!, 0.09);
      dummy.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), sp.tangent);
      dummy.updateMatrix();
      sleeperInstanced.setMatrixAt(i, dummy.matrix);
    }
    sleeperInstanced.instanceMatrix.needsUpdate = true;
    group.add(sleeperInstanced);

    // 3. Twin Continuous Steel Rails (UIC 60 standard profile)
    for (const sign of [-1, 1]) {
      const railPos: number[] = [];
      const railNorm: number[] = [];
      const railIndices: number[] = [];

      for (let s = 0; s < numSlices; s++) {
        const d = (s / (numSlices - 1)) * length;
        const sp = spline.getSampleAtDistance(d);
        const norm = sp.normal!;
        const binorm = sp.binormal!;

        const rx = sign * halfGauge;
        const rBaseY = 0.18; // atop sleeper
        const rHeadY = 0.35; // top of rail

        // Rail cross section (head top, web, base flange)
        const rPts = [
          { x: rx - 0.035, y: rHeadY },
          { x: rx + 0.035, y: rHeadY },
          { x: rx + 0.015, y: rBaseY + 0.04 },
          { x: rx - 0.015, y: rBaseY + 0.04 }
        ];

        for (const p of rPts) {
          const vx = sp.position.x + norm.x * p.x + binorm.x * p.y;
          const vy = sp.position.y + norm.y * p.x + binorm.y * p.y;
          const vz = sp.position.z + norm.z * p.x + binorm.z * p.y;
          railPos.push(vx, vy, vz);
          railNorm.push(0, 1, 0);
        }
      }

      for (let s = 0; s < numSlices - 1; s++) {
        const rowA = s * 4;
        const rowB = (s + 1) * 4;
        for (let j = 0; j < 3; j++) {
          railIndices.push(rowA + j, rowB + j, rowB + j + 1);
          railIndices.push(rowA + j, rowB + j + 1, rowA + j + 1);
        }
      }

      const railGeom = new THREE.BufferGeometry();
      railGeom.setAttribute('position', new THREE.Float32BufferAttribute(railPos, 3));
      railGeom.setAttribute('normal', new THREE.Float32BufferAttribute(railNorm, 3));
      railGeom.setIndex(railIndices);
      const railMesh = new THREE.Mesh(railGeom, materials.railwayRailMaterial);
      railMesh.castShadow = true;
      group.add(railMesh);
    }

    return group;
  }

  /**
   * At-grade Railroad Crossing: Rubber crossing pads + cantilever signal crossbucks + barrier gates.
   */
  public static buildCrossing(
    position: THREE.Vector3,
    railTangent: THREE.Vector3,
    roadWidth: number,
    materials: MaterialManager
  ): THREE.Group {
    const group = new THREE.Group();
    group.position.copy(position);

    const normal = new THREE.Vector3(railTangent.z, 0, -railTangent.x).normalize();

    // 1. Heavy Rubber & Concrete Flangeway Crossing Panels
    const padW = 2.4;
    const padL = roadWidth + 2.0;
    const padGeom = new THREE.BoxGeometry(padW, 0.16, padL);
    const padMat = new THREE.MeshStandardMaterial({ color: 0x2b2d30, roughness: 0.85 });
    const padMesh = new THREE.Mesh(padGeom, padMat);
    padMesh.position.set(0, 0.25, 0);
    padMesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), normal);
    group.add(padMesh);

    // 2. Cantilever Warning Signal Masts on both road approaches
    for (const sign of [-1, 1]) {
      const signalMast = new THREE.Group();
      signalMast.position.copy(new THREE.Vector3().addScaledVector(normal, sign * (roadWidth * 0.5 + 1.8)).addScaledVector(railTangent, 3.2));

      // Mast pole
      const poleGeom = new THREE.CylinderGeometry(0.12, 0.14, 5.5, 12);
      const poleMat = new THREE.MeshStandardMaterial({ color: 0xd0d5db, metalness: 0.7, roughness: 0.3 });
      const pole = new THREE.Mesh(poleGeom, poleMat);
      pole.position.y = 2.75;
      signalMast.add(pole);

      // Crossbuck "RAILROAD CROSSING"
      const crossbuckGroup = new THREE.Group();
      crossbuckGroup.position.y = 4.2;
      const bladeGeom = new THREE.BoxGeometry(1.4, 0.22, 0.04);
      const bladeMat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.4 });
      const blade1 = new THREE.Mesh(bladeGeom, bladeMat);
      blade1.rotation.z = Math.PI * 0.25;
      const blade2 = new THREE.Mesh(bladeGeom, bladeMat);
      blade2.rotation.z = -Math.PI * 0.25;
      crossbuckGroup.add(blade1, blade2);
      signalMast.add(crossbuckGroup);

      // Flashing Red LED Signal Lights
      for (const lx of [-0.45, 0.45]) {
        const lightHeadGeom = new THREE.CylinderGeometry(0.16, 0.16, 0.12, 12);
        lightHeadGeom.rotateX(Math.PI * 0.5);
        const headMat = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.5 });
        const headMesh = new THREE.Mesh(lightHeadGeom, headMat);
        headMesh.position.set(lx, 3.7, 0.15);

        // Red glowing lens
        const lensGeom = new THREE.CircleGeometry(0.13, 12);
        const lensMat = new THREE.MeshBasicMaterial({ color: 0xff1100 });
        const lensMesh = new THREE.Mesh(lensGeom, lensMat);
        lensMesh.position.set(0, 0, 0.07);
        headMesh.add(lensMesh);
        signalMast.add(headMesh);
      }

      // Motorized Striped Barrier Gate Arm
      const gatePivot = new THREE.Group();
      gatePivot.position.set(0, 1.2, 0.3);
      const gateArmGeom = new THREE.BoxGeometry(roadWidth * 0.55, 0.16, 0.05);
      const gateMat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.4 });
      const gateArm = new THREE.Mesh(gateArmGeom, gateMat);
      gateArm.position.set(-sign * (roadWidth * 0.27), 0, 0);
      gatePivot.add(gateArm);
      signalMast.add(gatePivot);

      group.add(signalMast);
    }

    return group;
  }

  /**
   * Builds high-fidelity modern commuter train (Locomotive + Passenger Coach)
   */
  public static buildCommuterTrain(): THREE.Group {
    const train = new THREE.Group();

    // 1. Locomotive Cab Car (Length 18m, Width 3.0m, Height 3.8m)
    const cab = new THREE.Group();
    const bodyGeom = new THREE.BoxGeometry(3.0, 3.2, 16.0);
    const bodyMat = new THREE.MeshStandardMaterial({ color: 0x1a5fb4, metalness: 0.6, roughness: 0.3 }); // sleek blue transit livery
    const bodyMesh = new THREE.Mesh(bodyGeom, bodyMat);
    bodyMesh.position.set(0, 2.3, 0);
    bodyMesh.castShadow = true;
    cab.add(bodyMesh);

    // Aerodynamic Nose Wedge
    const noseGeom = new THREE.ConeGeometry(2.1, 4.0, 4);
    noseGeom.rotateX(Math.PI * 0.5);
    noseGeom.rotateZ(Math.PI * 0.25);
    const noseMesh = new THREE.Mesh(noseGeom, bodyMat);
    noseMesh.position.set(0, 2.1, 9.5);
    cab.add(noseMesh);

    // Windshield & Windows
    const glassMat = new THREE.MeshStandardMaterial({ color: 0x152238, roughness: 0.1, metalness: 0.9 });
    const windshieldGeom = new THREE.PlaneGeometry(2.4, 1.1);
    const windshield = new THREE.Mesh(windshieldGeom, glassMat);
    windshield.position.set(0, 2.7, 10.2);
    windshield.rotation.x = -0.3;
    cab.add(windshield);

    // Headlights
    for (const hx of [-0.9, 0.9]) {
      const hlGeom = new THREE.CircleGeometry(0.18, 12);
      const hlMat = new THREE.MeshBasicMaterial({ color: 0xffffff });
      const hl = new THREE.Mesh(hlGeom, hlMat);
      hl.position.set(hx, 1.6, 10.8);
      cab.add(hl);
    }

    // Wheel Bogies (front & rear trucks)
    const bogieGeom = new THREE.BoxGeometry(2.4, 0.5, 3.4);
    const bogieMat = new THREE.MeshStandardMaterial({ color: 0x22262a, metalness: 0.8, roughness: 0.4 });
    for (const bz of [-5.5, 5.5]) {
      const bogie = new THREE.Mesh(bogieGeom, bogieMat);
      bogie.position.set(0, 0.6, bz);
      // Wheels
      const wheelGeom = new THREE.CylinderGeometry(0.45, 0.45, 0.14, 16);
      wheelGeom.rotateZ(Math.PI * 0.5);
      const wheelMat = new THREE.MeshStandardMaterial({ color: 0x888c94, metalness: 0.9, roughness: 0.2 });
      for (const wx of [-1.15, 1.15]) {
        for (const wz of [-1.1, 1.1]) {
          const wheel = new THREE.Mesh(wheelGeom, wheelMat);
          wheel.position.set(wx, 0.45, bz + wz);
          cab.add(wheel);
        }
      }
      cab.add(bogie);
    }

    // Pantograph atop roof
    const pantoGeom = new THREE.BoxGeometry(1.6, 0.6, 2.0);
    const pantoMat = new THREE.MeshStandardMaterial({ color: 0xd9534f, metalness: 0.8 });
    const panto = new THREE.Mesh(pantoGeom, pantoMat);
    panto.position.set(0, 4.1, -4.0);
    cab.add(panto);

    train.add(cab);

    // 2. Passenger Coach attached behind
    const coach = new THREE.Group();
    coach.position.set(0, 0, -17.5);
    const coachBodyMesh = new THREE.Mesh(bodyGeom, bodyMat);
    coachBodyMesh.position.set(0, 2.3, 0);
    coachBodyMesh.castShadow = true;
    coach.add(coachBodyMesh);

    // Continuous tinted passenger window strip
    const winStripGeom = new THREE.BoxGeometry(3.05, 0.85, 14.5);
    const winStrip = new THREE.Mesh(winStripGeom, glassMat);
    winStrip.position.set(0, 2.6, 0);
    coach.add(winStrip);

    // Coach bogies
    for (const bz of [-5.5, 5.5]) {
      const bogie = new THREE.Mesh(bogieGeom, bogieMat);
      bogie.position.set(0, 0.6, bz);
      coach.add(bogie);
    }

    train.add(coach);

    return train;
  }
}
