import * as THREE from 'three';
import { RoadSpline } from './spline';

export interface VehicleAgent {
  id: string;
  type: 'sedan' | 'suv' | 'coupe' | 'van' | 'bus';
  mesh: THREE.Group;
  spline: RoadSpline;
  distance: number;
  laneOffset: number; // lateral offset from road centerline
  speed: number; // m/s
  targetSpeed: number; // m/s
  maxSpeed: number;
  length: number;
  width: number;
  color: number;
  wheels: THREE.Mesh[];
  frontWheelPivots: THREE.Group[];
  brakeLightMesh: THREE.Mesh;
  headlightMesh: THREE.Mesh;
  headlights?: THREE.SpotLight[];
}

export class TrafficAISystem {
  public agents: VehicleAgent[] = [];
  public maxVehicles: number = 20; // Strictly <= 25 as requested!
  public group: THREE.Group = new THREE.Group();
  private splines: RoadSpline[] = [];
  private isNight: boolean = false;

  constructor() {
    this.group.name = 'ai_traffic_system';
  }

  public setSplines(splines: RoadSpline[]) {
    this.splines = splines.filter(s => s.totalLength > 15);
  }

  public setMaxVehicles(count: number) {
    this.maxVehicles = Math.min(25, Math.max(0, count));
    while (this.agents.length > this.maxVehicles) {
      const removed = this.agents.pop()!;
      this.group.remove(removed.mesh);
    }
  }

  public setNightMode(night: boolean) {
    this.isNight = night;
    for (const agent of this.agents) {
      if (agent.headlightMesh) {
        (agent.headlightMesh.material as THREE.MeshBasicMaterial).color.setHex(night ? 0xffffff : 0xcccccc);
      }
    }
  }

  /**
   * Spawns initial fleet of vehicles along available splines
   */
  public initFleet(count: number = 18) {
    this.clear();
    const spawnCount = Math.min(this.maxVehicles, count);
    if (this.splines.length === 0) return;

    const types: VehicleAgent['type'][] = ['sedan', 'suv', 'coupe', 'van', 'bus'];
    const colors = [0x22262b, 0xd0312d, 0xf0f3f6, 0x1a4480, 0x7c828d, 0x2e6930];

    for (let i = 0; i < spawnCount; i++) {
      const spline = this.splines[i % this.splines.length];
      const type = types[i % types.length];
      const color = colors[i % colors.length];

      // Lane direction: positive or negative offset from road center
      const laneOffset = (i % 2 === 0 ? 1.85 : -1.85);
      const initialDist = (i * (spline.totalLength / spawnCount)) % spline.totalLength;

      const agent = this.createVehicleAgent(`veh_${i}`, type, color, spline, initialDist, laneOffset);
      this.agents.push(agent);
      this.group.add(agent.mesh);
    }
  }

  public clear() {
    for (const agent of this.agents) {
      this.group.remove(agent.mesh);
    }
    this.agents = [];
  }

  /**
   * Update AI simulation tick (delta in seconds)
   */
  public update(delta: number) {
    const dt = Math.min(0.1, delta);

    for (let i = 0; i < this.agents.length; i++) {
      const agent = this.agents[i];
      const spline = agent.spline;

      // 1. Intelligent Driver Model (IDM) car-following
      // Look for leader vehicle in same lane ahead
      let distToLeader = Infinity;
      let leaderSpeed = agent.speed;

      for (let j = 0; j < this.agents.length; j++) {
        if (i === j) continue;
        const other = this.agents[j];
        if (other.spline === agent.spline && Math.sign(other.laneOffset) === Math.sign(agent.laneOffset)) {
          let sDiff = other.distance - agent.distance;
          if (sDiff < 0) sDiff += spline.totalLength; // wrap around
          if (sDiff > 0 && sDiff < distToLeader) {
            distToLeader = sDiff;
            leaderSpeed = other.speed;
          }
        }
      }

      // Safe following distance calculation (s* = s0 + v*T + (v*dv)/(2*sqrt(a*b)))
      const s0 = 5.0; // jam distance
      const T = 1.4;  // time headway
      const a = 2.0;  // max acceleration m/s^2
      const b = 3.0;  // comfortable deceleration m/s^2
      const deltaV = agent.speed - leaderSpeed;
      const desiredGap = s0 + agent.speed * T + (agent.speed * deltaV) / (2 * Math.sqrt(a * b));

      let accel = a * (1.0 - Math.pow(agent.speed / agent.targetSpeed, 4));
      if (distToLeader < Infinity) {
        accel -= a * Math.pow(desiredGap / Math.max(1.0, distToLeader), 2);
      }

      agent.speed = Math.max(0, agent.speed + accel * dt);
      const isBraking = accel < -0.8;

      // Update distance along spline
      agent.distance += agent.speed * dt;
      if (agent.distance >= spline.totalLength) {
        agent.distance -= spline.totalLength;
      }

      // 2. Position and Orient Vehicle along spline + lane offset
      const sp = spline.getSampleAtDistance(agent.distance);
      const norm = sp.normal!;
      const tan = sp.tangent!;

      const worldPos = new THREE.Vector3().copy(sp.position).addScaledVector(norm, agent.laneOffset);
      agent.mesh.position.copy(worldPos);

      // Alignment tangent (reverse if traveling backward in opposite lane)
      const travelDir = agent.laneOffset > 0 ? tan : new THREE.Vector3().copy(tan).negate();
      agent.mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), travelDir);

      // 3. Wheel rotation & Front steering
      const wheelRadius = agent.type === 'bus' ? 0.52 : (agent.type === 'suv' ? 0.38 : 0.32);
      const wheelRotSpeed = (agent.speed / wheelRadius) * dt;

      for (const wheel of agent.wheels) {
        wheel.rotation.x += wheelRotSpeed;
      }

      // Brake light glow when decelerating
      const brakeMat = agent.brakeLightMesh.material as THREE.MeshBasicMaterial;
      brakeMat.color.setHex(isBraking ? 0xff0000 : 0x550000);
    }
  }

  /**
   * Constructs high-fidelity 3D vehicle model
   */
  private createVehicleAgent(
    id: string,
    type: VehicleAgent['type'],
    color: number,
    spline: RoadSpline,
    dist: number,
    laneOffset: number
  ): VehicleAgent {
    const mesh = new THREE.Group();
    mesh.name = `vehicle_${id}`;

    const wheels: THREE.Mesh[] = [];
    const frontWheelPivots: THREE.Group[] = [];

    let length = 4.6;
    let width = 1.9;
    let height = 1.45;
    let maxSpeed = 16.0; // ~60 km/h

    if (type === 'suv') {
      length = 4.8;
      width = 2.0;
      height = 1.75;
      maxSpeed = 15.0;
    } else if (type === 'coupe') {
      length = 4.4;
      width = 1.85;
      height = 1.30;
      maxSpeed = 20.0;
    } else if (type === 'van') {
      length = 5.6;
      width = 2.1;
      height = 2.4;
      maxSpeed = 14.0;
    } else if (type === 'bus') {
      length = 11.5;
      width = 2.55;
      height = 3.2;
      maxSpeed = 12.0;
    }

    const bodyMat = new THREE.MeshStandardMaterial({
      color: type === 'bus' ? 0xcc3333 : color,
      metalness: 0.7,
      roughness: 0.25
    });

    const glassMat = new THREE.MeshStandardMaterial({
      color: 0x141a24,
      metalness: 0.9,
      roughness: 0.1
    });

    // 1. Main Car Body Chassis
    const chassisGeom = new THREE.BoxGeometry(width, height * 0.55, length);
    const chassis = new THREE.Mesh(chassisGeom, bodyMat);
    chassis.position.y = height * 0.45;
    chassis.castShadow = true;
    mesh.add(chassis);

    // 2. Cabin Roof & Glass Greenhouse
    if (type !== 'bus') {
      const cabinL = length * 0.52;
      const cabinW = width * 0.88;
      const cabinH = height * 0.5;
      const cabinGeom = new THREE.BoxGeometry(cabinW, cabinH, cabinL);
      const cabin = new THREE.Mesh(cabinGeom, glassMat);
      cabin.position.set(0, height * 0.82, -length * 0.06);
      cabin.castShadow = true;
      mesh.add(cabin);

      // Roof panel
      const roofGeom = new THREE.BoxGeometry(cabinW * 0.95, 0.05, cabinL * 0.9);
      const roof = new THREE.Mesh(roofGeom, bodyMat);
      roof.position.set(0, height * 0.82 + cabinH * 0.5 + 0.02, -length * 0.06);
      mesh.add(roof);
    } else {
      // Transit Bus Windows
      const winGeom = new THREE.BoxGeometry(width + 0.02, 1.1, length - 1.2);
      const win = new THREE.Mesh(winGeom, glassMat);
      win.position.set(0, 2.0, 0);
      mesh.add(win);
    }

    // 3. Headlights (Bright White LEDs)
    const hlGeom = new THREE.BoxGeometry(width * 0.82, 0.12, 0.05);
    const hlMat = new THREE.MeshBasicMaterial({ color: 0xffffff });
    const headlightMesh = new THREE.Mesh(hlGeom, hlMat);
    headlightMesh.position.set(0, height * 0.45, length * 0.5 + 0.02);
    mesh.add(headlightMesh);

    // 4. Taillights (Red Bar)
    const tlGeom = new THREE.BoxGeometry(width * 0.85, 0.10, 0.05);
    const tlMat = new THREE.MeshBasicMaterial({ color: 0x660000 });
    const brakeLightMesh = new THREE.Mesh(tlGeom, tlMat);
    brakeLightMesh.position.set(0, height * 0.48, -length * 0.5 - 0.02);
    mesh.add(brakeLightMesh);

    // 5. Wheels & Alloy Rims
    const wheelR = type === 'bus' ? 0.52 : (type === 'suv' ? 0.38 : 0.32);
    const wheelW = type === 'bus' ? 0.28 : 0.22;
    const wheelGeom = new THREE.CylinderGeometry(wheelR, wheelR, wheelW, 16);
    wheelGeom.rotateZ(Math.PI * 0.5);
    const tireMat = new THREE.MeshStandardMaterial({ color: 0x151618, roughness: 0.9 });
    const rimMat = new THREE.MeshStandardMaterial({ color: 0xd8dde6, metalness: 0.9, roughness: 0.2 });

    const wheelOffsetZ = length * 0.32;
    const wheelOffsetX = width * 0.5;

    // Four wheels
    for (const zSign of [-1, 1]) {
      for (const xSign of [-1, 1]) {
        const wheelPivot = new THREE.Group();
        wheelPivot.position.set(xSign * wheelOffsetX, wheelR, zSign * wheelOffsetZ);

        const tire = new THREE.Mesh(wheelGeom, tireMat);
        tire.castShadow = true;
        wheelPivot.add(tire);

        // Center hubcap / alloy rim
        const rimGeom = new THREE.CylinderGeometry(wheelR * 0.65, wheelR * 0.65, wheelW + 0.01, 12);
        rimGeom.rotateZ(Math.PI * 0.5);
        const rim = new THREE.Mesh(rimGeom, rimMat);
        wheelPivot.add(rim);

        mesh.add(wheelPivot);
        wheels.push(tire);

        if (zSign > 0) {
          frontWheelPivots.push(wheelPivot);
        }
      }
    }

    return {
      id,
      type,
      mesh,
      spline,
      distance: dist,
      laneOffset,
      speed: maxSpeed * 0.8,
      targetSpeed: maxSpeed,
      maxSpeed,
      length,
      width,
      color,
      wheels,
      frontWheelPivots,
      brakeLightMesh,
      headlightMesh
    };
  }
}
