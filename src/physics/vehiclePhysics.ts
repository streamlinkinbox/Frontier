import * as THREE from 'three';
import { clamp, lerp, smoothstep } from '../math/noise';
import {
  CrisscrossRootBridge,
  HollowLogTunnel,
  SurfaceType,
  TrailCorridorWorld,
  WorldObstacle,
} from '../world/trailCorridor';
import { VEHICLE_DESIGNS, VehicleDesignPreset } from '../vehicle/vehicleConfigs';

export interface VehicleControls {
  throttle: number;
  brake: number;
  steer: number;
  handbrake: boolean;
  boost: boolean;
}

export interface WheelState {
  index: number;
  name: 'FL' | 'FR' | 'RL' | 'RR';
  isFront: boolean;
  isLeft: boolean;
  mountLocal: THREE.Vector3;
  restLength: number;
  radius: number;

  compression: number;
  prevCompression: number;
  suspensionLength: number;
  inContact: boolean;
  normalLoad: number;
  steerAngle: number;
  angularVel: number;
  spinAngle: number;

  slipRatio: number;
  slipAngle: number;
  combinedSlip: number;
  surface: SurfaceType;
  mudiness: number;
  waterDepth: number;

  contactPointWorld: THREE.Vector3;
  contactNormalWorld: THREE.Vector3;
  hubPositionWorld: THREE.Vector3;
}

export interface ChassisHardpoint {
  id: string;
  label: string;
  localPos: THREE.Vector3;
  preferOverhead: boolean;
  isUndercarriage?: boolean;
}

export interface VehiclePhysicsConfig {
  gripMultiplier: number;
  suspensionStiffnessScale: number;
  enginePowerScale: number;
}

export const CHASSIS_HARDPOINTS: ReadonlyArray<ChassisHardpoint> = [
  { id: 'ROOF_FL', label: 'ROOF RACK FL', localPos: new THREE.Vector3(-0.62, 0.94, -0.45), preferOverhead: true },
  { id: 'ROOF_FR', label: 'ROOF RACK FR', localPos: new THREE.Vector3(0.62, 0.94, -0.45), preferOverhead: true },
  { id: 'ROOF_RL', label: 'ROLLCAGE RL', localPos: new THREE.Vector3(-0.64, 0.96, 0.58), preferOverhead: true },
  { id: 'ROOF_RR', label: 'ROLLCAGE RR', localPos: new THREE.Vector3(0.64, 0.96, 0.58), preferOverhead: true },
  { id: 'HOOD', label: 'COMPOSITE HOOD', localPos: new THREE.Vector3(0.0, 0.52, -1.35), preferOverhead: true },
  { id: 'BULLBAR', label: 'FRONT BULLBAR', localPos: new THREE.Vector3(0.0, 0.24, -2.08), preferOverhead: false, isUndercarriage: true },
  { id: 'PILLAR_L', label: 'A-PILLAR LEFT', localPos: new THREE.Vector3(-0.76, 0.68, -0.72), preferOverhead: false },
  { id: 'PILLAR_R', label: 'A-PILLAR RIGHT', localPos: new THREE.Vector3(0.76, 0.68, -0.72), preferOverhead: false },
  { id: 'AXLE_FL', label: 'FRONT LEFT AXLE', localPos: new THREE.Vector3(-0.88, -0.08, -1.38), preferOverhead: false, isUndercarriage: true },
  { id: 'AXLE_FR', label: 'FRONT RIGHT AXLE', localPos: new THREE.Vector3(0.88, -0.08, -1.38), preferOverhead: false, isUndercarriage: true },
  { id: 'AXLE_RL', label: 'REAR LEFT AXLE', localPos: new THREE.Vector3(-0.9, -0.08, 1.38), preferOverhead: false, isUndercarriage: true },
  { id: 'AXLE_RR', label: 'REAR RIGHT AXLE', localPos: new THREE.Vector3(0.9, -0.08, 1.38), preferOverhead: false, isUndercarriage: true },
  { id: 'REAR_SPARE', label: 'REAR SPARE CRADLE', localPos: new THREE.Vector3(0.0, 0.66, 1.72), preferOverhead: true },
];

const GEAR_RATIOS = [0, 3.35, 2.18, 1.56, 1.18, 0.94];
const REVERSE_RATIO = -3.25;

export class VehicleRigidBody {
  public readonly position = new THREE.Vector3(0, 2.0, 0);
  public readonly quaternion = new THREE.Quaternion();
  public readonly linearVelocity = new THREE.Vector3();
  public readonly angularVelocity = new THREE.Vector3();

  public currentPreset: VehicleDesignPreset = VEHICLE_DESIGNS[0];

  public mass = 1620;
  public invMass = 1 / 1620;
  private readonly bodyInvInertiaDiag = new THREE.Vector3(1 / 1240, 1 / 2850, 1 / 2620);
  private readonly worldInvInertia = new THREE.Matrix3();

  public readonly wheels: WheelState[] = [];
  private baseSpringRate = 42000;
  private bumpDamping = 4500;
  private reboundDamping = 6900;
  private antiRollStiffness = 12800;
  private peakTorqueNm = 600;
  private finalDrive = 6.3;

  public rpm = 1100;
  public gear = 1;
  public shiftCooldown = 0;
  public steerAngleCenter = 0;
  private prevSteerInput = 0;

  public fuel = 100.0;

  public boostCharge = 1.0;
  public isBoosting = false;

  public shredCharge = 1.0;
  public shredFlashTimer = 0;
  public wrenchIntensity = 0;
  public isInsideHollowLog = false;
  public isOnCrisscrossRoot = false;
  public isOnMudslide = false;
  public maxWheelWaterDepth = 0;

  public throttleLoad = 0;
  public forwardSpeedMps = 0;
  public speedKmh = 0;
  public lateralG = 0;
  public netVineForceWorld = new THREE.Vector3();
  public netVineTensionN = 0;
  public chassisMudAccumulation = 0.12;

  public config: VehiclePhysicsConfig = {
    gripMultiplier: 1.0,
    suspensionStiffnessScale: 1.0,
    enginePowerScale: 1.0,
  };

  private accumForce = new THREE.Vector3();
  private accumTorque = new THREE.Vector3();

  private _rotMat4 = new THREE.Matrix4();
  private _rotMat3 = new THREE.Matrix3();
  private _rotMat3T = new THREE.Matrix3();
  private _scaledMat3 = new THREE.Matrix3();
  private _upWorld = new THREE.Vector3();
  private _forwardWorld = new THREE.Vector3();
  private _rightWorld = new THREE.Vector3();
  private _mountWorld = new THREE.Vector3();
  private _rArm = new THREE.Vector3();
  private _velAtPoint = new THREE.Vector3();
  private _wheelFwd = new THREE.Vector3();
  private _wheelRight = new THREE.Vector3();
  private _fTotal = new THREE.Vector3();
  private _torqueTmp = new THREE.Vector3();
  private _suspTorque = new THREE.Vector3();
  private _terrainNormal = new THREE.Vector3();
  private _rootNormal = new THREE.Vector3();
  private _vortexForce = new THREE.Vector3();
  private _localPt = new THREE.Vector3();
  private _invQuat = new THREE.Quaternion();
  private _contactWorld = new THREE.Vector3();

  public onChassisImpact?: (intensity: number, worldPoint: THREE.Vector3) => void;
  /** When true (Observer / Peaceful Level Inspection), boulders, mudslides & dust devils never attack/shove the vehicle */
  public passiveObserverProtection = true;

  constructor() {
    const defs: Array<{ name: WheelState['name']; isFront: boolean; isLeft: boolean }> = [
      { name: 'FL', isFront: true, isLeft: true },
      { name: 'FR', isFront: true, isLeft: false },
      { name: 'RL', isFront: false, isLeft: true },
      { name: 'RR', isFront: false, isLeft: false },
    ];

    defs.forEach((d, index) => {
      this.wheels.push({
        index,
        name: d.name,
        isFront: d.isFront,
        isLeft: d.isLeft,
        mountLocal: new THREE.Vector3(),
        restLength: 0.64,
        radius: 0.46,
        compression: 0.22,
        prevCompression: 0.22,
        suspensionLength: 0.42,
        inContact: true,
        normalLoad: 4000,
        steerAngle: 0,
        angularVel: 0,
        spinAngle: 0,
        slipRatio: 0,
        slipAngle: 0,
        combinedSlip: 0,
        surface: 'PACKED_DIRT',
        mudiness: 0,
        waterDepth: 0,
        contactPointWorld: new THREE.Vector3(),
        contactNormalWorld: new THREE.Vector3(0, 1, 0),
        hubPositionWorld: new THREE.Vector3(),
      });
    });

    this.applyPreset(VEHICLE_DESIGNS[0]);
  }

  public applyPreset(preset: VehicleDesignPreset): void {
    this.currentPreset = preset;
    this.mass = preset.massKg;
    this.invMass = 1 / preset.massKg;
    this.bodyInvInertiaDiag.set(
      1 / preset.inertiaDiag[0],
      1 / preset.inertiaDiag[1],
      1 / preset.inertiaDiag[2]
    );

    this.baseSpringRate = preset.springRate;
    this.bumpDamping = preset.bumpDamping;
    this.reboundDamping = preset.reboundDamping;
    this.antiRollStiffness = preset.antiRollStiffness;
    this.peakTorqueNm = preset.peakTorqueNm;
    this.finalDrive = preset.finalDrive;

    for (const w of this.wheels) {
      const side = w.isLeft ? -1 : 1;
      const trackHalf = w.isFront ? preset.trackHalfF : preset.trackHalfR;
      const zPos = w.isFront ? preset.wheelbaseHalfF : preset.wheelbaseHalfR;
      w.mountLocal.set(side * trackHalf, 0.18, zPos);
      w.restLength = preset.suspRestLength;
      w.radius = preset.wheelRadius;
      w.compression = clamp(w.compression, 0.08, preset.suspRestLength * 0.5);
      w.suspensionLength = preset.suspRestLength - w.compression;
    }

    this.position.y += 0.14;
  }

  public triggerHydraulicShred(): boolean {
    if (this.shredCharge < 0.98) return false;
    this.shredCharge = 0.0;
    this.shredFlashTimer = 0.45;

    this._upWorld.set(0, 1, 0).applyQuaternion(this.quaternion);
    this._forwardWorld.set(0, 0, -1).applyQuaternion(this.quaternion);

    this.linearVelocity.y = Math.max(this.linearVelocity.y, 0) + 4.35;
    this.linearVelocity.addScaledVector(this._forwardWorld, 3.8);
    return true;
  }

  public resetAt(world: TrailCorridorWorld, z: number): void {
    let targetZ = z;
    let spec = world.getTrailSpec(targetZ);
    const branchSide = this.position.x > spec.centerX ? 1 : -1;
    let spawnX =
      spec.splitFactor > 0.25 ? spec.centerX + branchSide * spec.splitOffset : spec.centerX;
    let ground = world.sampleTerrain(spawnX, targetZ);

    // In CANYON_BRIDGES, if targetZ falls over open -48m chasm air (e.g. between diagonal bridges),
    // snap onto the nearest solid flat-topped Rock Pillar!
    if (world.currentBiome === 'CANYON_BRIDGES' && ground.height < -6.0) {
      const chunkIdx = Math.max(0, Math.floor(-targetZ / world.chunkLength));
      const uInChunk = -targetZ - chunkIdx * world.chunkLength;
      const pillarU = uInChunk < 30 ? 7.0 : uInChunk < 80 ? 53.0 : 104.5;
      targetZ = -(chunkIdx * world.chunkLength + pillarU);
      spec = world.getTrailSpec(targetZ);
      spawnX = spec.centerX;
      ground = world.sampleTerrain(spawnX, targetZ);
    }

    const eqHeight = this.currentPreset.suspRestLength * 0.62 + this.currentPreset.wheelRadius - 0.14;
    this.position.set(spawnX, ground.height + eqHeight, targetZ);

    const headingYaw = Math.atan2(spec.slopeX, 1.0);
    this.quaternion.setFromAxisAngle(new THREE.Vector3(0, 1, 0), headingYaw);

    this.linearVelocity.set(0, 0, 0);
    this.angularVelocity.set(0, 0, 0);
    this.rpm = 1150;
    this.gear = 1;
    this.steerAngleCenter = 0;
    this.boostCharge = 1.0;
    this.shredCharge = 1.0;
    this.shredFlashTimer = 0;
    this.isBoosting = false;
    this.isInsideHollowLog = false;
    this.isOnCrisscrossRoot = false;
    this.isOnMudslide = false;
    this.maxWheelWaterDepth = 0;
    this.accumForce.set(0, 0, 0);
    this.accumTorque.set(0, 0, 0);
    this.netVineForceWorld.set(0, 0, 0);
    this.netVineTensionN = 0;

    for (const w of this.wheels) {
      w.compression = w.restLength * 0.35;
      w.prevCompression = w.compression;
      w.angularVel = 0;
      w.slipRatio = 0;
      w.slipAngle = 0;
      w.combinedSlip = 0;
      w.waterDepth = 0;
    }
  }

  public localToWorld(localPt: THREE.Vector3, out = new THREE.Vector3()): THREE.Vector3 {
    return out.copy(localPt).applyQuaternion(this.quaternion).add(this.position);
  }

  public worldToLocal(worldPt: THREE.Vector3, out = new THREE.Vector3()): THREE.Vector3 {
    return out
      .copy(worldPt)
      .sub(this.position)
      .applyQuaternion(this._invQuat.copy(this.quaternion).invert());
  }

  public projectWorldPointOntoHull(worldPt: THREE.Vector3, outLocal = new THREE.Vector3()): THREE.Vector3 {
    this.worldToLocal(worldPt, outLocal);
    outLocal.x = clamp(outLocal.x, -0.98, 0.98);
    outLocal.z = clamp(outLocal.z, -2.1, 1.86);
    const maxRoofY = outLocal.z < -0.65 ? lerp(0.96, 0.46, clamp((-outLocal.z - 0.65) / 1.4, 0, 1)) : 1.0;
    outLocal.y = clamp(outLocal.y, -0.18, maxRoofY);
    return outLocal;
  }

  public testHullContact(worldPt: THREE.Vector3, padding = 0.24): boolean {
    this.worldToLocal(worldPt, this._localPt);
    const lx = this._localPt.x;
    const ly = this._localPt.y;
    const lz = this._localPt.z;

    if (Math.abs(lx) > 1.04 + padding) return false;
    if (lz < -2.15 - padding || lz > 1.92 + padding) return false;
    const roofProfile = lz < -0.65 ? lerp(1.02, 0.48, clamp((-lz - 0.65) / 1.45, 0, 1)) : 1.04;
    if (ly < -0.35 - padding || ly > roofProfile + padding) return false;
    return true;
  }

  public applyExternalForceAtLocalPoint(localPoint: THREE.Vector3, worldForce: THREE.Vector3): void {
    this.accumForce.add(worldForce);
    this._rArm.copy(localPoint).applyQuaternion(this.quaternion);
    this._torqueTmp.crossVectors(this._rArm, worldForce);
    this.accumTorque.add(this._torqueTmp);
  }

  private sampleEngineTorque(rpm: number): number {
    const r = clamp(rpm, 900, 7500);
    const peak = this.peakTorqueNm;
    let factor: number;
    if (r < 2200) {
      factor = lerp(0.55, 0.82, (r - 900) / 1300);
    } else if (r < 4800) {
      factor = lerp(0.82, 1.0, (r - 2200) / 2600);
    } else if (r < 6600) {
      factor = lerp(1.0, 0.88, (r - 4800) / 1800);
    } else {
      factor = lerp(0.88, 0.34, (r - 6600) / 900);
    }
    return peak * factor * this.config.enginePowerScale;
  }

  private updateWorldInertia(): void {
    this._rotMat4.makeRotationFromQuaternion(this.quaternion);
    this._rotMat3.setFromMatrix4(this._rotMat4);
    this._rotMat3T.copy(this._rotMat3).transpose();

    const e = this._rotMat3.elements;
    const ix = this.bodyInvInertiaDiag.x;
    const iy = this.bodyInvInertiaDiag.y;
    const iz = this.bodyInvInertiaDiag.z;

    this._scaledMat3.set(
      e[0] * ix, e[3] * iy, e[6] * iz,
      e[1] * ix, e[4] * iy, e[7] * iz,
      e[2] * ix, e[5] * iy, e[8] * iz
    );
    this.worldInvInertia.multiplyMatrices(this._scaledMat3, this._rotMat3T);
  }

  public stepPhysics(
    dt: number,
    controls: VehicleControls,
    world: TrailCorridorWorld,
    nearbyObstacles: WorldObstacle[],
    nearbyHollowLogs: HollowLogTunnel[] = [],
    nearbyRootBridges: CrisscrossRootBridge[] = []
  ): void {
    this.updateWorldInertia();

    this._rightWorld.set(1, 0, 0).applyQuaternion(this.quaternion);
    this._upWorld.set(0, 1, 0).applyQuaternion(this.quaternion);
    this._forwardWorld.set(0, 0, -1).applyQuaternion(this.quaternion);

    this.forwardSpeedMps = this.linearVelocity.dot(this._forwardWorld);
    const prevLatSpeed = this.linearVelocity.dot(this._rightWorld);
    this.speedKmh = Math.abs(this.forwardSpeedMps) * 3.6;

    this.shredCharge = Math.min(1.0, this.shredCharge + 0.195 * dt);
    this.shredFlashTimer = Math.max(0, this.shredFlashTimer - dt);

    const steerDeltaRate = Math.abs(controls.steer - this.prevSteerInput) / Math.max(dt, 1e-4);
    this.prevSteerInput = controls.steer;
    const driveEffort = Math.max(controls.throttle, controls.brake);
    const rawWrench =
      clamp(steerDeltaRate * 0.16, 0, 1.2) +
      Math.abs(controls.steer) * 0.35 +
      driveEffort * 0.48 +
      (controls.handbrake ? 0.35 : 0);
    this.wrenchIntensity = lerp(this.wrenchIntensity, clamp(rawWrench, 0, 1.5), clamp(8 * dt, 0, 1));

    // 1. Speed-Sensitive Steering + Rally Countersteer
    const absSpeed = Math.abs(this.forwardSpeedMps);
    const maxSteerAtSpeed = lerp(0.64, 0.29, clamp(absSpeed / 44, 0, 1));
    let targetSteer = -controls.steer * maxSteerAtSpeed;

    if (absSpeed > 4.0 && Math.abs(controls.steer) < 0.85) {
      const chassisSlipAngle = Math.atan2(prevLatSpeed, Math.max(absSpeed, 2.5));
      targetSteer -= clamp(chassisSlipAngle * 0.36, -0.24, 0.24);
    }

    const steerRate = Math.abs(targetSteer) > Math.abs(this.steerAngleCenter) ? 7.2 : 9.0;
    this.steerAngleCenter = lerp(this.steerAngleCenter, targetSteer, clamp(steerRate * dt, 0, 1));

    for (const w of this.wheels) {
      if (w.isFront) {
        const ackermann = w.isLeft
          ? this.steerAngleCenter > 0 ? 1.09 : 0.92
          : this.steerAngleCenter < 0 ? 1.09 : 0.92;
        w.steerAngle = this.steerAngleCenter * ackermann;
      } else {
        w.steerAngle = 0;
      }
    }

    // 2. Sequential Gearbox & Fuel Consumption!
    let activeThrottle = controls.throttle;
    let activeBrake = controls.brake;

    if (this.gear > 0) {
      if (controls.brake > 0.2 && controls.throttle < 0.05 && this.forwardSpeedMps < 1.3) {
        this.gear = -1;
      }
    } else if (this.gear === -1) {
      if (controls.throttle > 0.2 && this.forwardSpeedMps > -1.8) {
        this.gear = 1;
      } else {
        activeThrottle = controls.brake;
        activeBrake = controls.throttle;
      }
    }

    if (controls.boost && this.boostCharge > 0.04 && activeThrottle > 0.1 && this.fuel > 1.0) {
      this.isBoosting = true;
      this.boostCharge = Math.max(0, this.boostCharge - 0.32 * dt);
    } else {
      this.isBoosting = false;
      const slipBonus = (this.wheels[2].combinedSlip + this.wheels[3].combinedSlip) * 0.035;
      const waterCoolBonus = this.maxWheelWaterDepth > 0.03 ? 0.07 : 0;
      this.boostCharge = Math.min(
        1.0,
        this.boostCharge + (0.12 + slipBonus + waterCoolBonus) * this.currentPreset.boostRechargeScale * dt
      );
    }

    // Engine Fuel Burn (disabled in passive observer protection!)
    if (!this.passiveObserverProtection) {
      const burnRate = activeThrottle * (this.isBoosting ? 1.25 : 0.42);
      this.fuel = Math.max(0, this.fuel - burnRate * dt);
    }

    // 3. Gravity + Aero Drag + Robotic Tick Parasitic Brake + Desert Dust Devil Swirl!
    this.accumForce.y -= this.mass * 9.81;

    const speedSq = this.linearVelocity.lengthSq();
    if (speedSq > 0.01) {
      const speed = Math.sqrt(speedSq);
      const dragMag = 0.5 * 1.225 * 0.86 * speedSq;
      this.accumForce.addScaledVector(this.linearVelocity, -dragMag / speed);
      const downforce = 0.5 * 1.225 * 0.68 * speedSq;
      this.accumForce.addScaledVector(this._upWorld, -downforce);
    }

    if (!this.passiveObserverProtection && world.sampleDustDevilForce(this.position, this._vortexForce)) {
      this.accumForce.add(this._vortexForce);
    }

    // 4. Suspension Raycast + Crisscrossing Giant Root Bridges + Hollow Logs + Mudslide Push!
    const springRate = this.baseSpringRate * this.config.suspensionStiffnessScale;
    let insideAnyLog = false;
    let onAnyRootBridge = false;
    let onAnyMudslide = false;
    let deepestWater = 0;

    for (const w of this.wheels) {
      this.localToWorld(w.mountLocal, this._mountWorld);
      const maxRay = w.restLength + w.radius;

      const probeX = this._mountWorld.x - this._upWorld.x * w.restLength;
      const probeZ = this._mountWorld.z - this._upWorld.z * w.restLength;
      const sample = world.sampleTerrain(probeX, probeZ, this._terrainNormal);

      let groundY = sample.height;
      let wheelSurf: SurfaceType = sample.surface;
      let wheelWaterDepth = sample.waterDepth;

      // A. Static Crisscrossing Giant Root Bridges (drive ON TOP OF elevated roots OR pass underneath!)
      if (nearbyRootBridges.length > 0) {
        const rootDeckY = world.sampleCrisscrossRootDeck(
          probeX,
          probeZ,
          this._mountWorld.y,
          nearbyRootBridges,
          this._rootNormal
        );
        if (rootDeckY !== null && rootDeckY > groundY) {
          groundY = rootDeckY;
          wheelSurf = 'CRISSCROSS_ROOT';
          wheelWaterDepth = 0;
          onAnyRootBridge = true;
          this._terrainNormal.copy(this._rootNormal);
        }
      }

      // B. Drive-Through Hollow Fallen Log Tunnels
      for (const hl of nearbyHollowLogs) {
        const rightX = -hl.axisZ;
        const rightZ = hl.axisX;
        const dx = probeX - hl.center.x;
        const dz = probeZ - hl.center.z;
        const along = dx * hl.axisX + dz * hl.axisZ;
        const lat = dx * rightX + dz * rightZ;

        if (Math.abs(along) < hl.halfLength + 2.4 && Math.abs(lat) < hl.innerRadius - 0.16) {
          const rSq = hl.innerRadius * hl.innerRadius - lat * lat;
          if (rSq > 0.25 && this.position.y < hl.center.y + hl.innerRadius * 0.85) {
            const cylFloorY = hl.center.y - Math.sqrt(rSq);
            const rampBlend = smoothstep(hl.halfLength + 2.4, hl.halfLength - 1.8, Math.abs(along));
            const blendedY = lerp(groundY, Math.max(groundY, cylFloorY), rampBlend);
            if (blendedY >= groundY - 0.05) {
              groundY = blendedY;
              wheelSurf = 'HOLLOW_LOG';
              wheelWaterDepth = 0;
              insideAnyLog = true;

              const sinLat = clamp(lat / hl.innerRadius, -0.75, 0.75) * rampBlend;
              const cosLat = Math.sqrt(Math.max(0.2, 1.0 - sinLat * sinLat));
              this._terrainNormal
                .set(-rightX * sinLat * 0.78, cosLat, -rightZ * sinLat * 0.78)
                .normalize();
            }
          }
        }
      }

      // C. Fallen Logs & Toppled Trees
      for (const obs of nearbyObstacles) {
        if (obs.kind === 'fallen_log' && obs.halfLength && obs.axisX !== undefined && obs.axisZ !== undefined) {
          if (obs.underClearanceY !== undefined && obs.underClearanceY > this.position.y + 0.72) {
            continue;
          }
          const dx = probeX - obs.position.x;
          const dz = probeZ - obs.position.z;
          const along = clamp(dx * obs.axisX + dz * obs.axisZ, -obs.halfLength, obs.halfLength);
          const cx = obs.position.x + obs.axisX * along;
          const cz = obs.position.z + obs.axisZ * along;
          const distSq = (probeX - cx) * (probeX - cx) + (probeZ - cz) * (probeZ - cz);
          const rLog = obs.radius + 0.14;
          if (distSq < rLog * rLog) {
            const logProfileY = obs.position.y + Math.sqrt(Math.max(0, rLog * rLog - distSq));
            if (logProfileY > groundY && logProfileY < this._mountWorld.y + 0.65) {
              groundY = logProfileY;
              this._terrainNormal.set((probeX - cx) * 0.8, 1.0, (probeZ - cz) * 0.8).normalize();
            }
          }
        }
      }

      const upCos = Math.max(0.35, this._upWorld.y);
      const distToGroundAlongSuspension = (this._mountWorld.y - groundY) / upCos;

      w.prevCompression = w.compression;
      w.surface = wheelSurf;
      w.mudiness = wheelSurf === 'HOLLOW_LOG' || wheelSurf === 'CRISSCROSS_ROOT' ? 0 : sample.mudiness;
      w.waterDepth = wheelWaterDepth;
      if (wheelWaterDepth > deepestWater) deepestWater = wheelWaterDepth;

      if (distToGroundAlongSuspension < maxRay && this._upWorld.y > 0.15) {
        w.inContact = true;
        const suspLen = clamp(distToGroundAlongSuspension - w.radius, 0.06, w.restLength);
        w.suspensionLength = suspLen;
        w.compression = w.restLength - suspLen;

        const compVel = (w.compression - w.prevCompression) / dt;
        const bumpStop = Math.max(0, w.compression - (w.restLength - 0.16));
        const fSpring = springRate * w.compression + 380000 * bumpStop * bumpStop;
        const damper = compVel >= 0 ? this.bumpDamping : this.reboundDamping;
        const fDamper = damper * compVel;

        w.normalLoad = clamp(fSpring + fDamper, 0, this.mass * 9.81 * 3.2);
        w.contactNormalWorld.copy(this._terrainNormal);
        w.hubPositionWorld.copy(this._mountWorld).addScaledVector(this._upWorld, -suspLen);
        w.contactPointWorld.copy(w.hubPositionWorld).addScaledVector(this._upWorld, -w.radius);

        // Flash-Flood Mudslide Lateral Push (disabled in passive observer protection!)
        if (
          !this.passiveObserverProtection &&
          wheelSurf === 'MUDSLIDE' &&
          Math.abs(sample.mudslidePushX) > 0.1
        ) {
          onAnyMudslide = true;
          const pushN = sample.mudslidePushX * (w.isFront ? 340 : 510);
          this._vortexForce.set(pushN, 0, 0);
          this.applyExternalForceAtLocalPoint(w.mountLocal, this._vortexForce);
        }
      } else {
        w.inContact = false;
        w.suspensionLength = lerp(w.suspensionLength, w.restLength, clamp(12 * dt, 0, 1));
        w.compression = w.restLength - w.suspensionLength;
        w.normalLoad = 0;
        w.waterDepth = 0;
        w.hubPositionWorld.copy(this._mountWorld).addScaledVector(this._upWorld, -w.suspensionLength);
        w.contactPointWorld.copy(w.hubPositionWorld).addScaledVector(this._upWorld, -w.radius);
      }
    }

    this.isInsideHollowLog = insideAnyLog;
    this.isOnCrisscrossRoot = onAnyRootBridge;
    this.isOnMudslide = onAnyMudslide;
    this.maxWheelWaterDepth = deepestWater;

    // 5. Anti-Roll Bars
    const applyAntiRoll = (wL: WheelState, wR: WheelState) => {
      const diff = wL.compression - wR.compression;
      const arbForce = diff * this.antiRollStiffness;
      if (wL.inContact) wL.normalLoad = Math.max(0, wL.normalLoad + arbForce);
      if (wR.inContact) wR.normalLoad = Math.max(0, wR.normalLoad - arbForce);
    };
    applyAntiRoll(this.wheels[0], this.wheels[1]);
    applyAntiRoll(this.wheels[2], this.wheels[3]);

    // 6. Drivetrain & Fuel / Parasite Modifiers
    const gearRatio = this.gear === -1 ? REVERSE_RATIO : GEAR_RATIOS[this.gear];
    const avgDrivenWheelOmega =
      this.wheels[0].angularVel * 0.18 +
      this.wheels[1].angularVel * 0.18 +
      this.wheels[2].angularVel * 0.32 +
      this.wheels[3].angularVel * 0.32;

    const wheelCoupledRpm =
      Math.abs(avgDrivenWheelOmega * gearRatio * this.finalDrive) * (60 / (2 * Math.PI));
    const freeRevRpm = 1050 + activeThrottle * 5800;
    const clutchLock = clamp(Math.abs(this.forwardSpeedMps) / 3.5, 0.25, 1.0);
    const targetRpm = clamp(lerp(freeRevRpm, Math.max(1050, wheelCoupledRpm), clutchLock), 1000, 7400);
    this.rpm = lerp(this.rpm, targetRpm, clamp(18 * dt, 0, 1));
    this.throttleLoad = activeThrottle;

    this.shiftCooldown = Math.max(0, this.shiftCooldown - dt);
    const refRadius = this.currentPreset.wheelRadius;
    if (this.gear > 0 && this.shiftCooldown <= 0) {
      const anyGrounded =
        this.wheels[2].inContact ||
        this.wheels[3].inContact ||
        this.wheels[0].inContact ||
        this.wheels[1].inContact;
      const wheelSpeedEquivRpm =
        Math.abs((this.forwardSpeedMps / refRadius) * gearRatio * this.finalDrive) *
        (60 / (2 * Math.PI));
      if (this.gear < 5 && anyGrounded && wheelSpeedEquivRpm > 5850) {
        this.gear++;
        this.shiftCooldown = 0.24;
      } else if (this.gear > 1 && wheelSpeedEquivRpm < 3050) {
        this.gear--;
        this.shiftCooldown = 0.2;
      }
    }

    const fuelPowerMult = this.fuel <= 0.1 ? 0.48 : 1.0;
    const boostTorqueMult = this.isBoosting ? 1.85 : 1.0;
    const engineTorque =
      this.sampleEngineTorque(this.rpm) *
      activeThrottle *
      boostTorqueMult *
      fuelPowerMult;
    const engineBraking = (1 - activeThrottle) * clamp((this.rpm - 1200) / 6000, 0, 1) * 85;
    const totalAxleDriveTorque = (engineTorque - engineBraking) * gearRatio * this.finalDrive * 0.58;

    if (this.isBoosting) {
      this.accumForce.addScaledVector(this._forwardWorld, 5.2 * this.mass);
    }

    // 7. Per-Wheel Combined-Slip Pacejka Tire Solver
    const wheelInertia = 1.75;
    const nominalCornerLoad = (this.mass * 9.81) * 0.25;

    for (const w of this.wheels) {
      const torqueShare = w.isFront ? 0.18 : 0.32;
      const driveTorque = totalAxleDriveTorque * torqueShare;
      const maxBrakeTorque = w.isFront ? 3100 : 2300;
      let brakeTorque = activeBrake * maxBrakeTorque;
      if (controls.handbrake && !w.isFront) {
        brakeTorque = 5200;
      }

      if (!w.inContact || w.normalLoad <= 1.0) {
        const brakeSign = -Math.sign(w.angularVel);
        w.angularVel += ((driveTorque + brakeSign * brakeTorque * 0.35) / wheelInertia) * dt;
        w.angularVel = clamp(w.angularVel * (1 - 0.5 * dt), -95, 135);
        w.spinAngle += w.angularVel * dt;
        w.slipRatio = 0;
        w.slipAngle = 0;
        w.combinedSlip = 0;
        continue;
      }

      this._rArm.copy(w.contactPointWorld).sub(this.position);
      this._velAtPoint.crossVectors(this.angularVelocity, this._rArm).add(this.linearVelocity);

      const cosS = Math.cos(w.steerAngle);
      const sinS = Math.sin(w.steerAngle);
      this._wheelFwd.set(-sinS, 0, -cosS).applyQuaternion(this.quaternion);
      const nDotF = this._wheelFwd.dot(w.contactNormalWorld);
      this._wheelFwd.addScaledVector(w.contactNormalWorld, -nDotF).normalize();
      this._wheelRight.crossVectors(this._wheelFwd, w.contactNormalWorld).normalize();

      const vLong = this._velAtPoint.dot(this._wheelFwd);
      const vLat = this._velAtPoint.dot(this._wheelRight);

      const surfSample = world.sampleTerrain(w.contactPointWorld.x, w.contactPointWorld.z);
      const baseMu =
        w.surface === 'HOLLOW_LOG' || w.surface === 'CRISSCROSS_ROOT'
          ? 1.04
          : surfSample.friction;
      const mu = baseMu * this.currentPreset.gripBias * this.config.gripMultiplier;

      w.angularVel += (driveTorque / wheelInertia) * dt;
      if (controls.handbrake && !w.isFront) {
        w.angularVel = lerp(w.angularVel, 0, clamp(32 * dt, 0, 1));
      } else if (brakeTorque > 1) {
        const dOmega = (brakeTorque / wheelInertia) * dt;
        if (Math.abs(w.angularVel) <= dOmega) w.angularVel = 0;
        else w.angularVel -= Math.sign(w.angularVel) * dOmega;
      }

      const denomLong = Math.max(Math.abs(vLong), 2.1);
      const rawSlipRatio = clamp((w.angularVel * w.radius - vLong) / denomLong, -1.6, 1.6);
      const denomLat = Math.max(Math.abs(vLong), 1.8);
      const rawSlipAngle = Math.atan2(vLat, denomLat);

      w.slipRatio = rawSlipRatio;
      w.slipAngle = rawSlipAngle;

      const loadSensitivity = Math.pow(nominalCornerLoad / Math.max(w.normalLoad, 650), 0.12);
      const fMax = mu * w.normalLoad * loadSensitivity;

      const sx = rawSlipRatio / 0.16;
      const sy = Math.tan(clamp(rawSlipAngle, -1.25, 1.25)) / 0.21;
      const sMag = Math.sqrt(sx * sx + sy * sy);

      let fLong = 0;
      let fLat = 0;

      if (sMag > 1e-4) {
        const B = 1.15;
        const C = 1.38;
        const E = 0.22;
        const bs = B * sMag;
        const pacejkaFactor = Math.sin(C * Math.atan(bs - E * (bs - Math.atan(bs))));
        const forceMag = fMax * pacejkaFactor;

        fLong = forceMag * (sx / sMag);
        const latBias = controls.handbrake && !w.isFront ? 0.68 : w.isFront ? 1.06 : 1.12;
        fLat = -forceMag * (sy / sMag) * clamp(latBias, 0.5, 1.15);
      }

      const lowSpeedBlend = 1 - clamp(Math.hypot(vLong, vLat) / 2.2, 0, 1);
      if (lowSpeedBlend > 0) {
        fLat = lerp(fLat, clamp(-vLat * 5400, -fMax, fMax), lowSpeedBlend * 0.75);
        if (activeThrottle < 0.05 && activeBrake > 0.1) {
          fLong = lerp(fLong, clamp(-vLong * 6200, -fMax, fMax), lowSpeedBlend * 0.85);
        }
      }

      const fRoll = -vLong * surfSample.rollingResistance * w.normalLoad * 0.24;
      const hydroDrag =
        w.waterDepth > 0.02
          ? clamp(-vLong * Math.abs(vLong) * w.waterDepth * 32, -3400, 3400)
          : 0;
      fLong += fRoll + hydroDrag;

      const roadReactionTorque = -fLong * w.radius;
      w.angularVel += (roadReactionTorque / wheelInertia) * dt;
      if (activeThrottle < 0.05 && !controls.handbrake && activeBrake < 0.05) {
        w.angularVel = lerp(w.angularVel, vLong / w.radius, clamp(9 * dt, 0, 1));
      }
      w.angularVel = clamp(w.angularVel, -95, 145);
      w.spinAngle += w.angularVel * dt;

      w.combinedSlip = clamp(Math.hypot(rawSlipRatio * 0.85, rawSlipAngle * 1.35), 0, 1.5);

      const fSuspVec = this._torqueTmp
        .copy(w.contactNormalWorld)
        .lerp(this._upWorld, 0.35)
        .normalize()
        .multiplyScalar(w.normalLoad);

      this.accumForce.add(fSuspVec);
      this._rArm.copy(w.hubPositionWorld).sub(this.position);
      this._suspTorque.crossVectors(this._rArm, fSuspVec);
      this.accumTorque.add(this._suspTorque);

      this._fTotal.set(0, 0, 0);
      this._fTotal.addScaledVector(this._wheelFwd, fLong);
      this._fTotal.addScaledVector(this._wheelRight, fLat);
      this.accumForce.add(this._fTotal);

      this._rArm.addScaledVector(this._upWorld, 0.17);
      this._torqueTmp.crossVectors(this._rArm, this._fTotal);
      this.accumTorque.add(this._torqueTmp);
    }

    // 8. Chassis Belly Clearance & SPACIOUS Wide Excursion Envelope (±90m!)
    const chassisTerrain = world.sampleTerrain(this.position.x, this.position.z);
    const minChassisY = chassisTerrain.height + 0.44;
    if (this.position.y < minChassisY) {
      const pen = minChassisY - this.position.y;
      this.position.y = minChassisY;
      const vn = this.linearVelocity.dot(chassisTerrain.normal);
      if (vn < 0) {
        this.linearVelocity.addScaledVector(chassisTerrain.normal, -1.18 * vn);
        this.linearVelocity.multiplyScalar(0.985);
      }
      this.accumForce.addScaledVector(chassisTerrain.normal, pen * 95000);
    }

    if (this._upWorld.y < 0.38) {
      const rightingAxis = this._torqueTmp.crossVectors(this._upWorld, new THREE.Vector3(0, 1, 0));
      this.accumTorque.addScaledVector(rightingAxis, (0.45 - this._upWorld.y) * 44000);
    }

    const maxForestExcursion = chassisTerrain.trailHalfWidth + 42.0;
    if (Math.abs(chassisTerrain.signedOffset) > maxForestExcursion) {
      const sign = Math.sign(chassisTerrain.signedOffset);
      const excess = Math.abs(chassisTerrain.signedOffset) - maxForestExcursion;
      this.accumForce.x -= sign * excess * 38000;
      if (this.linearVelocity.x * sign > 0) {
        this.linearVelocity.x *= 0.95;
      }
    }

    // 9. Obstacle & Hollow-Log Shell Collisions
    this.resolveObstacleCollisions(nearbyObstacles);
    this.resolveHollowLogWalls(nearbyHollowLogs);

    // 10. Integrate 6-DOF Momentum
    this.linearVelocity.addScaledVector(this.accumForce, this.invMass * dt);
    const maxLinearSpeed = 60;
    if (this.linearVelocity.lengthSq() > maxLinearSpeed * maxLinearSpeed) {
      this.linearVelocity.setLength(maxLinearSpeed);
    }
    this.position.addScaledVector(this.linearVelocity, dt);

    const angAccel = this._torqueTmp.copy(this.accumTorque).applyMatrix3(this.worldInvInertia);
    this.angularVelocity.addScaledVector(angAccel, dt);
    this.angularVelocity.multiplyScalar(Math.exp(-1.7 * dt));
    const maxAngVel = 5.2;
    if (this.angularVelocity.lengthSq() > maxAngVel * maxAngVel) {
      this.angularVelocity.setLength(maxAngVel);
    }

    const wx = this.angularVelocity.x;
    const wy = this.angularVelocity.y;
    const wz = this.angularVelocity.z;
    const qx = this.quaternion.x;
    const qy = this.quaternion.y;
    const qz = this.quaternion.z;
    const qw = this.quaternion.w;

    const halfDt = 0.5 * dt;
    this.quaternion.set(
      qx + (wx * qw + wy * qz - wz * qy) * halfDt,
      qy + (-wx * qz + wy * qw + wz * qx) * halfDt,
      qz + (wx * qy - wy * qx + wz * qw) * halfDt,
      qw + (-wx * qx - wy * qy - wz * qz) * halfDt
    );
    this.quaternion.normalize();

    const newLatSpeed = this.linearVelocity.dot(this._rightWorld);
    this.lateralG = clamp((newLatSpeed - prevLatSpeed) / (dt * 9.81), -3.5, 3.5);

    this.accumForce.set(0, 0, 0);
    this.accumTorque.set(0, 0, 0);
  }

  private resolveHollowLogWalls(hollowLogs: HollowLogTunnel[]): void {
    const carHalfW = 1.05;
    for (const hl of hollowLogs) {
      const rightX = -hl.axisZ;
      const rightZ = hl.axisX;
      const dx = this.position.x - hl.center.x;
      const dz = this.position.z - hl.center.z;
      const along = dx * hl.axisX + dz * hl.axisZ;
      if (Math.abs(along) > hl.halfLength + 0.6) continue;
      if (this.position.y > hl.center.y + hl.outerRadius) continue;

      const lat = dx * rightX + dz * rightZ;
      const absLat = Math.abs(lat);
      const signLat = Math.sign(lat) || 1;
      const midShell = (hl.innerRadius + hl.outerRadius) * 0.5;

      if (absLat < midShell && absLat > hl.innerRadius - carHalfW) {
        const pushIn = absLat - (hl.innerRadius - carHalfW);
        this.position.x -= rightX * signLat * pushIn * 0.9;
        this.position.z -= rightZ * signLat * pushIn * 0.9;
        const vLat = this.linearVelocity.x * rightX + this.linearVelocity.z * rightZ;
        if (vLat * signLat > 0) {
          this.linearVelocity.x -= rightX * vLat * 1.15;
          this.linearVelocity.z -= rightZ * vLat * 1.15;
        }
      } else if (absLat >= midShell && absLat < hl.outerRadius + carHalfW) {
        const pushOut = hl.outerRadius + carHalfW - absLat;
        this.position.x += rightX * signLat * pushOut * 0.9;
        this.position.z += rightZ * signLat * pushOut * 0.9;
        const vLat = this.linearVelocity.x * rightX + this.linearVelocity.z * rightZ;
        if (vLat * signLat < 0) {
          this.linearVelocity.x -= rightX * vLat * 1.15;
          this.linearVelocity.z -= rightZ * vLat * 1.15;
        }
      }
    }
  }

  private resolveObstacleCollisions(obstacles: WorldObstacle[]): void {
    const hullSpheres = [
      { zOff: -1.35, rad: 0.98 },
      { zOff: 0.0, rad: 1.05 },
      { zOff: 1.22, rad: 1.0 },
    ];

    for (const obs of obstacles) {
      if (obs.kind === 'fallen_log') continue;
      if (this.passiveObserverProtection && obs.kind === 'rolling_boulder') continue;
      if (this.position.y > obs.position.y + obs.height + 0.5) continue;

      for (const hs of hullSpheres) {
        const sx = this.position.x + this._forwardWorld.x * -hs.zOff;
        const sz = this.position.z + this._forwardWorld.z * -hs.zOff;

        const dx = sx - obs.position.x;
        const dz = sz - obs.position.z;
        const minDist = hs.rad + obs.radius;
        const distSq = dx * dx + dz * dz;

        if (distSq < minDist * minDist && distSq > 1e-6) {
          const dist = Math.sqrt(distSq);
          const nx = dx / dist;
          const nz = dz / dist;
          const overlap = minDist - dist;

          this.position.x += nx * overlap * 0.86;
          this.position.z += nz * overlap * 0.86;

          const vn = this.linearVelocity.x * nx + this.linearVelocity.z * nz;
          if (vn < 0) {
            const restitution = obs.kind === 'rolling_boulder' ? 0.55 : 0.24;
            const jMag = -(1 + restitution) * vn + (obs.kind === 'rolling_boulder' ? 5.5 : 0);
            this.linearVelocity.x += nx * jMag;
            this.linearVelocity.z += nz * jMag;
            this.linearVelocity.multiplyScalar(0.95);

            const leverZ = hs.zOff;
            const localHitX = nx * this._rightWorld.x + nz * this._rightWorld.z;
            this.angularVelocity.y += clamp(-leverZ * localHitX * jMag * 0.085, -1.8, 1.8);

            if (Math.abs(vn) > 2.4 && this.onChassisImpact) {
              this._contactWorld.set(obs.position.x + nx * obs.radius, this.position.y, obs.position.z + nz * obs.radius);
              this.onChassisImpact(clamp(Math.abs(vn) / 18, 0.2, 1.0), this._contactWorld);
            }
          }
        }
      }
    }
  }
}
