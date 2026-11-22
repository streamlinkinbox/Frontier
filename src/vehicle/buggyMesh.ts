import * as THREE from 'three';
import { clamp, lerp } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { VehicleRigidBody } from '../physics/vehiclePhysics';
import { VEHICLE_DESIGNS, VehicleDesignId, VehicleDesignPreset } from './vehicleConfigs';

interface ArticulatedCornerRig {
  steerPivot: THREE.Group;
  wheelSpinGroup: THREE.Group;
  upperArm: THREE.Mesh;
  lowerArm: THREE.Mesh;
  coiloverShock: THREE.Mesh;
  chassisUpperMount: THREE.Vector3;
  chassisLowerMount: THREE.Vector3;
  chassisShockTower: THREE.Vector3;
}

export class FrontierBuggyVisual {
  public readonly root = new THREE.Group();
  public readonly chassisGroup = new THREE.Group();

  private bodyVariantGroups = new Map<VehicleDesignId, THREE.Group>();
  private cornerRigs: ArticulatedCornerRig[] = [];

  private brakeLightMat: THREE.MeshStandardMaterial;
  private headlightEmissiveMat: THREE.MeshStandardMaterial;
  private shockMat: THREE.MeshStandardMaterial;
  private exhaustFlameL: THREE.Mesh;
  private exhaustFlameR: THREE.Mesh;
  private shredWaveRing: THREE.Mesh;
  private shredWaveMat: THREE.MeshBasicMaterial;
  private whipAntennaPivot: THREE.Group;
  private headSpotLight: THREE.SpotLight;
  private headSpotTarget: THREE.Object3D;

  private prevVel = new THREE.Vector3();
  private antennaAngles = new THREE.Vector2(0, 0);
  private antennaVel = new THREE.Vector2(0, 0);

  constructor(textures: ProceduralTextureLibrary, creepAttachmentGroup: THREE.Group) {
    this.root.add(this.chassisGroup);
    this.chassisGroup.add(creepAttachmentGroup);

    this.headlightEmissiveMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0xfff6e0),
      emissive: new THREE.Color(0xffe8b8),
      emissiveIntensity: 3.3,
      roughness: 0.15,
    });

    this.brakeLightMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x550808),
      emissive: new THREE.Color(0xff1e10),
      emissiveIntensity: 0.9,
      roughness: 0.25,
    });

    this.shockMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0xc8962e),
      roughness: 0.25,
      metalness: 0.88,
    });

    const tireMat = new THREE.MeshStandardMaterial({
      map: textures.tireTread.map,
      normalMap: textures.tireTread.normalMap,
      normalScale: new THREE.Vector2(1.85, 1.85),
      roughness: 0.86,
      metalness: 0.05,
    });

    const sharedRimMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x272b2e),
      roughness: 0.36,
      metalness: 0.8,
    });

    const sharedAccentMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0xd4832a),
      roughness: 0.35,
      metalness: 0.3,
    });

    const wheelProto = this.createDetailedWheelMesh(tireMat, sharedRimMat, sharedAccentMat);

    // Pre-build all 4 distinct AA Vehicle Body Shells for instant 0ms live switching!
    for (const preset of VEHICLE_DESIGNS) {
      const variantGroup = this.buildDesignShell(preset, wheelProto);
      variantGroup.visible = preset.id === 'NOMAD';
      this.chassisGroup.add(variantGroup);
      this.bodyVariantGroups.set(preset.id, variantGroup);
    }

    // Shared Twin Afterburner Plumes
    const flameMat = new THREE.MeshBasicMaterial({
      color: 0xff9d3b,
      transparent: true,
      opacity: 0.85,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });
    const flameGeo = new THREE.ConeGeometry(0.12, 0.82, 8);
    flameGeo.rotateX(Math.PI * 0.5);
    flameGeo.translate(0, 0, 0.41);

    this.exhaustFlameL = new THREE.Mesh(flameGeo, flameMat);
    this.exhaustFlameL.position.set(-0.38, 0.36, 1.88);
    this.exhaustFlameL.visible = false;
    this.chassisGroup.add(this.exhaustFlameL);

    this.exhaustFlameR = new THREE.Mesh(flameGeo, flameMat);
    this.exhaustFlameR.position.set(0.38, 0.36, 1.88);
    this.exhaustFlameR.visible = false;
    this.chassisGroup.add(this.exhaustFlameR);

    // Hydraulic Root-Shredder Expanding Shockwave Ring ([E] VFX!)
    const ringGeo = new THREE.RingGeometry(0.85, 1.25, 28);
    ringGeo.rotateX(-Math.PI * 0.5);
    this.shredWaveMat = new THREE.MeshBasicMaterial({
      color: 0xffba49,
      side: THREE.DoubleSide,
      transparent: true,
      opacity: 0,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });
    this.shredWaveRing = new THREE.Mesh(ringGeo, this.shredWaveMat);
    this.shredWaveRing.position.set(0, -0.1, 0);
    this.shredWaveRing.visible = false;
    this.chassisGroup.add(this.shredWaveRing);

    // Whip Antenna
    this.whipAntennaPivot = new THREE.Group();
    this.whipAntennaPivot.position.set(-0.66, 0.96, 0.88);
    const antGeo = new THREE.CylinderGeometry(0.008, 0.02, 1.45, 6);
    antGeo.translate(0, 0.725, 0);
    const antMesh = new THREE.Mesh(
      antGeo,
      new THREE.MeshStandardMaterial({ color: 0x1f2224, roughness: 0.5 })
    );
    const flagGeo = new THREE.PlaneGeometry(0.26, 0.14);
    flagGeo.translate(0.13, 1.38, 0);
    const flagMesh = new THREE.Mesh(
      flagGeo,
      new THREE.MeshStandardMaterial({ color: 0xe06d1b, side: THREE.DoubleSide, roughness: 0.6 })
    );
    this.whipAntennaPivot.add(antMesh, flagMesh);
    this.chassisGroup.add(this.whipAntennaPivot);

    // Forward Rally Spotlight
    this.headSpotLight = new THREE.SpotLight(0xfff1d0, 30.0, 66, Math.PI * 0.3, 0.52, 1.3);
    this.headSpotLight.position.set(0, 1.02, -0.55);
    this.headSpotTarget = new THREE.Object3D();
    this.headSpotTarget.position.set(0, -0.45, -20.0);
    this.chassisGroup.add(this.headSpotLight, this.headSpotTarget);
    this.headSpotLight.target = this.headSpotTarget;

    // 4 Articulated Long-Travel Suspension Corners
    const armGeo = new THREE.CylinderGeometry(0.034, 0.038, 1.0, 7);
    armGeo.rotateX(Math.PI * 0.5);
    const shockGeo = new THREE.CylinderGeometry(0.058, 0.05, 1.0, 8);
    shockGeo.rotateX(Math.PI * 0.5);

    const wheelDefs = [
      { isLeft: true, isFront: true },
      { isLeft: false, isFront: true },
      { isLeft: true, isFront: false },
      { isLeft: false, isFront: false },
    ];

    wheelDefs.forEach((wd) => {
      const steerPivot = new THREE.Group();
      const wheelSpinGroup = wheelProto.clone();
      if (!wd.isLeft) wheelSpinGroup.rotation.y = Math.PI;
      steerPivot.add(wheelSpinGroup);
      this.chassisGroup.add(steerPivot);

      const upperArm = new THREE.Mesh(armGeo, sharedRimMat);
      const lowerArm = new THREE.Mesh(armGeo, sharedRimMat);
      const coiloverShock = new THREE.Mesh(shockGeo, this.shockMat);
      upperArm.castShadow = true;
      lowerArm.castShadow = true;
      coiloverShock.castShadow = true;
      this.chassisGroup.add(upperArm, lowerArm, coiloverShock);

      this.cornerRigs.push({
        steerPivot,
        wheelSpinGroup,
        upperArm,
        lowerArm,
        coiloverShock,
        chassisUpperMount: new THREE.Vector3(),
        chassisLowerMount: new THREE.Vector3(),
        chassisShockTower: new THREE.Vector3(),
      });
    });

    this.setVehicleDesign(VEHICLE_DESIGNS[0]);
  }

  public setVehicleDesign(preset: VehicleDesignPreset): void {
    for (const [id, grp] of this.bodyVariantGroups.entries()) {
      grp.visible = id === preset.id;
    }

    this.shockMat.color.setHex(preset.shockHex);

    // Scale 4 articulated wheels & suspension hardpoints to match the selected vehicle design!
    const rScale = preset.wheelRadius / 0.45;
    const wScale = preset.wheelWidth / 0.34;

    this.cornerRigs.forEach((rig, idx) => {
      const isLeft = idx % 2 === 0;
      const isFront = idx < 2;
      const side = isLeft ? -1 : 1;
      const z = isFront ? preset.wheelbaseHalfF : preset.wheelbaseHalfR;

      rig.wheelSpinGroup.scale.set(wScale, rScale, rScale);
      rig.chassisUpperMount.set(side * 0.46, 0.16, z);
      rig.chassisLowerMount.set(side * 0.4, -0.14, z);
      rig.chassisShockTower.set(side * 0.54, 0.5, z + (isFront ? 0.08 : -0.08));
    });
  }

  private buildDesignShell(preset: VehicleDesignPreset, wheelProto: THREE.Group): THREE.Group {
    const group = new THREE.Group();

    const primaryMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(preset.primaryHex),
      roughness: 0.42,
      metalness: 0.28,
    });
    const accentMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(preset.secondaryHex),
      roughness: 0.36,
      metalness: 0.26,
    });
    const cageMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(preset.cageHex),
      roughness: 0.35,
      metalness: 0.8,
    });
    const carbonMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x1b1e20),
      roughness: 0.52,
      metalness: 0.35,
    });
    const alloyMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0xaab2b6),
      roughness: 0.28,
      metalness: 0.88,
    });

    const addTube = (pts: THREE.Vector3[], rad = 0.036, mat: THREE.Material = cageMat) => {
      const curve = new THREE.CatmullRomCurve3(pts);
      const geo = new THREE.TubeGeometry(curve, 12, rad, 7, false);
      const m = new THREE.Mesh(geo, mat);
      m.castShadow = true;
      group.add(m);
    };

    // Armored Underbelly Pan common base
    const belly = new THREE.Mesh(new THREE.BoxGeometry(1.22, 0.26, 3.4), carbonMat);
    belly.position.set(0, -0.06, -0.04);
    belly.castShadow = true;
    group.add(belly);

    if (preset.id === 'NOMAD') {
      // DESIGN 1: NOMAD T1+ RALLY-RAID EXO BUGGY
      const profile = new THREE.Shape();
      profile.moveTo(-1.96, 0.12);
      profile.lineTo(-1.72, 0.44);
      profile.lineTo(-0.68, 0.56);
      profile.lineTo(-0.35, 0.62);
      profile.lineTo(0.82, 0.62);
      profile.lineTo(1.58, 0.52);
      profile.lineTo(1.82, 0.28);
      profile.lineTo(1.62, 0.04);
      profile.lineTo(-1.78, 0.02);
      profile.closePath();

      const bodyGeo = this.extrudeTaperedBody(profile, 1.32, 0.66);
      const mainBody = new THREE.Mesh(bodyGeo, primaryMat);
      mainBody.castShadow = true;
      mainBody.receiveShadow = true;
      group.add(mainBody);

      for (const side of [-1, 1]) {
        const pod = new THREE.Mesh(new THREE.BoxGeometry(0.32, 0.34, 1.48), primaryMat);
        pod.position.set(side * 0.78, 0.28, 0.12);
        pod.rotation.z = side * -0.12;
        pod.castShadow = true;
        group.add(pod);

        const stripe = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.07, 1.25), accentMat);
        stripe.position.set(side * 0.79, 0.34, 0.1);
        stripe.rotation.z = side * -0.12;
        group.add(stripe);

        addTube([
          new THREE.Vector3(side * 0.52, 0.48, -1.15),
          new THREE.Vector3(side * 0.62, 0.95, -0.45),
          new THREE.Vector3(side * 0.64, 0.96, 0.58),
          new THREE.Vector3(side * 0.54, 0.52, 1.65),
        ]);
        addTube([
          new THREE.Vector3(side * 0.34, 1.0, 0.12),
          new THREE.Vector3(side * 0.34, 1.0, 0.13),
        ]);
        const board = new THREE.Mesh(new THREE.BoxGeometry(0.28, 0.034, 0.9), accentMat);
        board.position.set(side * 0.34, 1.0, 0.12);
        group.add(board);
      }

      const roofCap = new THREE.Mesh(new THREE.BoxGeometry(1.22, 0.055, 1.18), carbonMat);
      roofCap.position.set(0, 0.96, 0.08);
      group.add(roofCap);

      addTube([
        new THREE.Vector3(-0.46, 0.14, -1.92),
        new THREE.Vector3(-0.38, 0.44, -2.08),
        new THREE.Vector3(0.38, 0.44, -2.08),
        new THREE.Vector3(0.46, 0.14, -1.92),
      ], 0.04);

      const barLens = new THREE.Mesh(new THREE.BoxGeometry(1.0, 0.055, 0.04), this.headlightEmissiveMat);
      barLens.position.set(0, 1.01, -0.47);
      group.add(barLens);

      const spare = wheelProto.clone();
      spare.scale.setScalar(0.88);
      spare.rotation.set(Math.PI * 0.33, 0, Math.PI * 0.5);
      spare.position.set(0, 0.66, 1.62);
      group.add(spare);
    } else if (preset.id === 'TITAN') {
      // DESIGN 2: TITAN V8 WIDEBODY BAJA TROPHY PICKUP TRUCK
      const profile = new THREE.Shape();
      profile.moveTo(-2.14, 0.18); // tall muscular Baja grille
      profile.lineTo(-2.06, 0.66); // vertical trophy truck nose!
      profile.lineTo(-0.82, 0.72); // long flat V8 hood
      profile.lineTo(-0.32, 1.08); // cab windshield rake
      profile.lineTo(0.48, 1.08); // crew-cab roof
      profile.lineTo(0.62, 0.68); // cab back drop into open trophy bed!
      profile.lineTo(1.94, 0.66); // flared rear trophy fenders
      profile.lineTo(1.98, 0.24);
      profile.lineTo(-1.98, 0.08);
      profile.closePath();

      const bodyGeo = this.extrudeTaperedBody(profile, 1.72, 0.92);
      const mainBody = new THREE.Mesh(bodyGeo, primaryMat);
      mainBody.castShadow = true;
      mainBody.receiveShadow = true;
      group.add(mainBody);

      // Widebody flared Baja fenders + gold racing heritage stripe
      for (const side of [-1, 1]) {
        const fFender = new THREE.Mesh(new THREE.BoxGeometry(0.24, 0.28, 1.05), primaryMat);
        fFender.position.set(side * 0.96, 0.48, -1.48);
        fFender.castShadow = true;
        group.add(fFender);

        const rFender = new THREE.Mesh(new THREE.BoxGeometry(0.26, 0.3, 1.15), primaryMat);
        rFender.position.set(side * 0.98, 0.48, 1.38);
        rFender.castShadow = true;
        group.add(rFender);

        const stripe = new THREE.Mesh(new THREE.BoxGeometry(1.86 * 0.02 + 0.04, 0.09, 3.65), accentMat);
        stripe.position.set(side * 0.91, 0.56, -0.05);
        group.add(stripe);

        // Bed diagonal trophy chase-rack bars
        addTube([
          new THREE.Vector3(side * 0.72, 1.06, 0.45),
          new THREE.Vector3(side * 0.76, 0.68, 1.78),
        ], 0.042);
      }

      // Supercharged V8 Hood Blower Scoop!
      const blowerBase = new THREE.Mesh(new THREE.BoxGeometry(0.48, 0.16, 0.68), alloyMat);
      blowerBase.position.set(0, 0.76, -1.28);
      group.add(blowerBase);
      const blowerIntake = new THREE.Mesh(new THREE.BoxGeometry(0.42, 0.1, 0.12), this.brakeLightMat);
      blowerIntake.position.set(0, 0.8, -1.61);
      group.add(blowerIntake);

      // 4 Round Baja Floodlight Pods on Front Pre-Runner Bumper!
      addTube([
        new THREE.Vector3(-0.78, 0.24, -2.08),
        new THREE.Vector3(-0.62, 0.58, -2.22),
        new THREE.Vector3(0.62, 0.58, -2.22),
        new THREE.Vector3(0.78, 0.24, -2.08),
      ], 0.045);

      const podGeo = new THREE.CylinderGeometry(0.1, 0.1, 0.09, 12);
      podGeo.rotateX(Math.PI * 0.5);
      for (const xOff of [-0.42, -0.14, 0.14, 0.42]) {
        const pod = new THREE.Mesh(podGeo, this.headlightEmissiveMat);
        pod.position.set(xOff, 0.48, -2.18);
        group.add(pod);
      }

      // Dual Upright Full-Size Spare Mud Tires mounted side-by-side in open bed!
      for (const side of [-1, 1]) {
        const sp = wheelProto.clone();
        sp.scale.setScalar(0.92);
        sp.position.set(side * 0.26, 0.78, 1.28);
        sp.rotation.x = -0.25;
        group.add(sp);
      }
    } else if (preset.id === 'MANTIS') {
      // DESIGN 3: MANTIS RS — BLADE-FENDER CANOPY INTERCEPTOR
      const profile = new THREE.Shape();
      profile.moveTo(-2.18, 0.08); // razor needle nose!
      profile.lineTo(-1.35, 0.38);
      profile.lineTo(-0.45, 0.88); //fighter canopy bubble
      profile.lineTo(0.35, 0.88);
      profile.lineTo(1.25, 0.56); // tapered engine spine
      profile.lineTo(1.76, 0.36);
      profile.lineTo(1.62, 0.05);
      profile.lineTo(-1.95, 0.03);
      profile.closePath();

      const bodyGeo = this.extrudeTaperedBody(profile, 1.08, 0.42);
      const mainBody = new THREE.Mesh(bodyGeo, primaryMat);
      mainBody.castShadow = true;
      mainBody.receiveShadow = true;
      group.add(mainBody);

      // Tinted fighter-canopy visor dome
      const canopyVisor = new THREE.Mesh(new THREE.BoxGeometry(0.82, 0.26, 0.92), carbonMat);
      canopyVisor.position.set(0, 0.78, -0.06);
      canopyVisor.rotation.x = 0.14;
      group.add(canopyVisor);

      // SIGNATURE FEATURE: Forward-Swept Metallic Vine & Root Cutter Blades on Front & A-Pillars!
      const cutterBladeGeo = new THREE.BoxGeometry(0.03, 0.56, 0.22);
      for (const side of [-1, 1]) {
        const pillarBlade = new THREE.Mesh(cutterBladeGeo, alloyMat);
        pillarBlade.position.set(side * 0.52, 0.68, -0.78);
        pillarBlade.rotation.x = -0.68;
        pillarBlade.rotation.z = side * -0.15;
        group.add(pillarBlade);

        // Wheel-arch root-slicing scythe fairings!
        const fenderScythe = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.06, 0.88), accentMat);
        fenderScythe.position.set(side * 0.96, 0.52, -1.42);
        fenderScythe.rotation.x = 0.12;
        group.add(fenderScythe);

        // Side Venturi intake tunnels
        const pod = new THREE.Mesh(new THREE.BoxGeometry(0.38, 0.26, 1.32), carbonMat);
        pod.position.set(side * 0.68, 0.24, 0.22);
        group.add(pod);
      }

      // High-Downforce Dual-Pylon Rally Raid Rear Wing!
      const wingBlade = new THREE.Mesh(new THREE.BoxGeometry(1.58, 0.05, 0.34), accentMat);
      wingBlade.position.set(0, 1.04, 1.56);
      wingBlade.rotation.x = 0.16;
      wingBlade.castShadow = true;
      group.add(wingBlade);

      for (const side of [-1, 1]) {
        const pylon = new THREE.Mesh(new THREE.BoxGeometry(0.04, 0.48, 0.26), cageMat);
        pylon.position.set(side * 0.44, 0.78, 1.52);
        group.add(pylon);
      }

      // Exposed Twin-Turbo Intercooler Core
      const intercooler = new THREE.Mesh(new THREE.BoxGeometry(0.68, 0.24, 0.42), alloyMat);
      intercooler.position.set(0, 0.62, 0.92);
      intercooler.rotation.x = -0.35;
      group.add(intercooler);

      const noseLed = new THREE.Mesh(new THREE.BoxGeometry(0.68, 0.05, 0.06), this.headlightEmissiveMat);
      noseLed.position.set(0, 0.24, -2.02);
      group.add(noseLed);
    } else {
      // DESIGN 4: IRONCLAD 4x4 — PORTAL-AXLE ARMORED JUNGLE OVERLAND CRAWLER
      const profile = new THREE.Shape();
      profile.moveTo(-2.02, 0.24); // high-clearance approach angle!
      profile.lineTo(-1.92, 0.72); // armored flat grille
      profile.lineTo(-0.85, 0.78); // boxy armored hood
      profile.lineTo(-0.52, 1.18); // steep armored split windshield
      profile.lineTo(1.52, 1.18); // full-length armored expedition wagon roof!
      profile.lineTo(1.78, 0.72); // angled tactical rear hatch
      profile.lineTo(1.72, 0.26); // high departure angle!
      profile.lineTo(-1.82, 0.16);
      profile.closePath();

      const bodyGeo = this.extrudeTaperedBody(profile, 1.62, 0.94);
      const mainBody = new THREE.Mesh(bodyGeo, primaryMat);
      mainBody.castShadow = true;
      mainBody.receiveShadow = true;
      group.add(mainBody);

      // Heavy Steel Winch Bumper + Cable Drum
      const bumper = new THREE.Mesh(new THREE.BoxGeometry(1.78, 0.24, 0.32), cageMat);
      bumper.position.set(0, 0.32, -2.02);
      bumper.castShadow = true;
      group.add(bumper);

      const winchDrum = new THREE.Mesh(
        new THREE.CylinderGeometry(0.09, 0.09, 0.42, 10).rotateZ(Math.PI * 0.5),
        alloyMat
      );
      winchDrum.position.set(0, 0.36, -2.16);
      group.add(winchDrum);

      // Raised Right-Side Safari Snorkel Intake!
      addTube([
        new THREE.Vector3(0.84, 0.72, -0.95),
        new THREE.Vector3(0.88, 0.82, -0.62),
        new THREE.Vector3(0.86, 1.32, -0.45),
      ], 0.052, cageMat);

      // Full-Length Expedition Roof Basket + Brass Rescue Beacons + Jerrycans!
      const basketFrame = new THREE.Mesh(new THREE.BoxGeometry(1.48, 0.12, 1.82), cageMat);
      basketFrame.position.set(0, 1.24, 0.48);
      group.add(basketFrame);

      for (const side of [-1, 1]) {
        const jerry = new THREE.Mesh(new THREE.BoxGeometry(0.22, 0.3, 0.42), accentMat);
        jerry.position.set(side * 0.52, 1.34, 0.85);
        group.add(jerry);

        const hl = new THREE.Mesh(
          new THREE.CylinderGeometry(0.11, 0.11, 0.08, 12).rotateX(Math.PI * 0.5),
          this.headlightEmissiveMat
        );
        hl.position.set(side * 0.58, 0.54, -1.98);
        group.add(hl);
      }

      const roofBar = new THREE.Mesh(new THREE.BoxGeometry(1.24, 0.065, 0.06), this.headlightEmissiveMat);
      roofBar.position.set(0, 1.26, -0.44);
      group.add(roofBar);

      // Rear-Door Mounted Spare Tire
      const rearSpare = wheelProto.clone();
      rearSpare.scale.setScalar(0.86);
      rearSpare.rotation.y = Math.PI * 0.5;
      rearSpare.position.set(0, 0.74, 1.88);
      group.add(rearSpare);
    }

    // Common Front Skidplate, Rear LED Brake Strip & Twin Titanium Exhausts
    const skid = new THREE.Mesh(new THREE.BoxGeometry(0.92, 0.05, 0.68), alloyMat);
    skid.position.set(0, 0.06, -1.88);
    skid.rotation.x = 0.46;
    group.add(skid);

    const tailBar = new THREE.Mesh(new THREE.BoxGeometry(1.24, 0.07, 0.06), this.brakeLightMat);
    tailBar.position.set(0, 0.52, 1.82);
    group.add(tailBar);

    const exGeo = new THREE.CylinderGeometry(0.055, 0.055, 0.28, 10);
    exGeo.rotateX(Math.PI * 0.5);
    for (const side of [-1, 1]) {
      const ex = new THREE.Mesh(exGeo, alloyMat);
      ex.position.set(side * 0.38, 0.36, 1.82);
      group.add(ex);
    }

    return group;
  }

  private extrudeTaperedBody(
    profile: THREE.Shape,
    width: number,
    noseTaperRatio: number
  ): THREE.BufferGeometry {
    const geo = new THREE.ExtrudeGeometry(profile, {
      depth: width,
      bevelEnabled: true,
      bevelSegments: 2,
      steps: 1,
      bevelSize: 0.065,
      bevelThickness: 0.065,
    });
    geo.rotateY(-Math.PI * 0.5);
    geo.translate(width * 0.5, 0, 0);

    const pos = geo.attributes.position;
    for (let i = 0; i < pos.count; i++) {
      const z = pos.getZ(i);
      const x = pos.getX(i);
      if (z < -0.65) {
        const taper = lerp(1.0, noseTaperRatio, clamp((-z - 0.65) / 1.45, 0, 1));
        pos.setX(i, x * taper);
      }
    }
    geo.computeVertexNormals();
    return geo;
  }

  private createDetailedWheelMesh(
    tireMat: THREE.Material,
    rimMat: THREE.Material,
    caliperMat: THREE.Material
  ): THREE.Group {
    const group = new THREE.Group();
    const radius = 0.45;
    const width = 0.34;

    const treadGeo = new THREE.CylinderGeometry(radius, radius, width * 0.82, 24, 1);
    treadGeo.rotateZ(Math.PI * 0.5);
    const tread = new THREE.Mesh(treadGeo, tireMat);
    tread.castShadow = true;
    tread.receiveShadow = true;
    group.add(tread);

    for (const s of [-1, 1]) {
      const shoulderGeo = new THREE.CylinderGeometry(radius * 0.94, radius, width * 0.1, 24);
      shoulderGeo.rotateZ(Math.PI * 0.5);
      shoulderGeo.translate(s * width * 0.45, 0, 0);
      group.add(new THREE.Mesh(shoulderGeo, tireMat));
    }

    const rimBarrelGeo = new THREE.CylinderGeometry(radius * 0.58, radius * 0.58, width * 0.88, 16);
    rimBarrelGeo.rotateZ(Math.PI * 0.5);
    group.add(new THREE.Mesh(rimBarrelGeo, rimMat));

    const beadGeo = new THREE.TorusGeometry(radius * 0.57, 0.022, 8, 20);
    beadGeo.rotateY(Math.PI * 0.5);
    beadGeo.translate(-width * 0.44, 0, 0);
    group.add(new THREE.Mesh(beadGeo, caliperMat));

    const spokeGeo = new THREE.BoxGeometry(0.035, radius * 1.05, 0.055);
    spokeGeo.translate(-width * 0.38, 0, 0);
    for (let i = 0; i < 3; i++) {
      const spoke = new THREE.Mesh(spokeGeo, rimMat);
      spoke.rotation.x = (i / 3) * Math.PI;
      group.add(spoke);
    }

    return group;
  }

  private _hubLocal = new THREE.Vector3();
  private _tmpDir = new THREE.Vector3();
  private _mid = new THREE.Vector3();
  private _worldTarget = new THREE.Vector3();

  public syncWithPhysics(vehicle: VehicleRigidBody, dt: number, isBraking: boolean): void {
    this.chassisGroup.position.copy(vehicle.position);
    this.chassisGroup.quaternion.copy(vehicle.quaternion);

    this.brakeLightMat.emissiveIntensity = isBraking ? 4.2 : 0.85;

    const showFlames = vehicle.isBoosting;
    this.exhaustFlameL.visible = showFlames;
    this.exhaustFlameR.visible = showFlames;
    if (showFlames) {
      const flicker = 0.8 + Math.random() * 0.55;
      this.exhaustFlameL.scale.set(1, 1, flicker);
      this.exhaustFlameR.scale.set(1, 1, flicker * 0.95);
    }

    // Animate [E] Hydraulic Root-Shredder Shockwave Ring!
    if (vehicle.shredFlashTimer > 0) {
      this.shredWaveRing.visible = true;
      const prog = 1.0 - vehicle.shredFlashTimer / 0.45; // 0 -> 1
      const scale = lerp(1.0, 4.6, prog);
      this.shredWaveRing.scale.set(scale, scale, 1);
      this.shredWaveMat.opacity = (1.0 - prog) * 0.85;
    } else {
      this.shredWaveRing.visible = false;
    }

    const safeDt = Math.max(dt, 1 / 120);
    const accelWorldX = (vehicle.linearVelocity.x - this.prevVel.x) / safeDt;
    const accelWorldZ = (vehicle.linearVelocity.z - this.prevVel.z) / safeDt;
    this.prevVel.copy(vehicle.linearVelocity);

    const targetSwayX = clamp(accelWorldZ * 0.018, -0.55, 0.55);
    const targetSwayZ = clamp(-accelWorldX * 0.018, -0.55, 0.55);
    this.antennaVel.x += ((targetSwayX - this.antennaAngles.x) * 48 - this.antennaVel.x * 5.2) * safeDt;
    this.antennaVel.y += ((targetSwayZ - this.antennaAngles.y) * 48 - this.antennaVel.y * 5.2) * safeDt;
    this.antennaAngles.x += this.antennaVel.x * safeDt;
    this.antennaAngles.y += this.antennaVel.y * safeDt;
    this.whipAntennaPivot.rotation.set(this.antennaAngles.x, 0, this.antennaAngles.y);

    for (let i = 0; i < 4; i++) {
      const w = vehicle.wheels[i];
      const rig = this.cornerRigs[i];

      this._hubLocal.set(w.mountLocal.x, w.mountLocal.y - w.suspensionLength, w.mountLocal.z);
      rig.steerPivot.position.copy(this._hubLocal);
      rig.steerPivot.rotation.set(0, w.steerAngle, 0);

      if (w.isLeft) {
        rig.wheelSpinGroup.rotation.set(-w.spinAngle, 0, 0);
      } else {
        rig.wheelSpinGroup.rotation.set(w.spinAngle, Math.PI, 0);
      }

      const hubUpper = this._tmpDir.set(this._hubLocal.x * 0.88, this._hubLocal.y + 0.11, this._hubLocal.z);
      this.orientStrutBetween(rig.upperArm, rig.chassisUpperMount, hubUpper);

      const hubLower = this._tmpDir.set(this._hubLocal.x * 0.9, this._hubLocal.y - 0.1, this._hubLocal.z);
      this.orientStrutBetween(rig.lowerArm, rig.chassisLowerMount, hubLower);

      const shockBottom = this._tmpDir.set(
        lerp(rig.chassisLowerMount.x, this._hubLocal.x, 0.72),
        lerp(rig.chassisLowerMount.y, this._hubLocal.y - 0.05, 0.72),
        this._hubLocal.z
      );
      this.orientStrutBetween(rig.coiloverShock, rig.chassisShockTower, shockBottom);
    }
  }

  private orientStrutBetween(mesh: THREE.Mesh, aLocal: THREE.Vector3, bLocal: THREE.Vector3): void {
    const len = aLocal.distanceTo(bLocal);
    this._mid.addVectors(aLocal, bLocal).multiplyScalar(0.5);
    mesh.position.copy(this._mid);
    mesh.scale.set(1, 1, Math.max(0.05, len));
    this._worldTarget.copy(bLocal).sub(aLocal).normalize();
    mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), this._worldTarget);
  }
}
