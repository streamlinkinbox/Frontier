import * as THREE from 'three';
import { clamp, lerp, mulberry32 } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import {
  BIOME_SPECS,
  BiomeId,
  CrisscrossRootBridge,
  TrailCorridorWorld,
} from '../world/trailCorridor';
import { VehicleRigidBody } from '../physics/vehiclePhysics';
import { VEHICLE_DESIGNS, VehicleDesignPreset } from '../vehicle/vehicleConfigs';
import { FrontierBuggyVisual } from '../vehicle/buggyMesh';

export type RacePhase = 'TEAM_ELIMINATION' | 'SOLO_SHOWDOWN' | 'FINISHED';

export interface TeamInfo {
  id: string;
  name: string;
  colorHex: string;
  isPlayerTeam: boolean;
  eliminated: boolean;
  tallyScore: number;
  memberRanks: number[];
}

export interface RacerEntry {
  id: string;
  callsign: string;
  teamId: string;
  teamName: string;
  teamColorHex: string;
  isPlayer: boolean;
  isTeammate: boolean;
  eliminated: boolean;
  rank: number; // 1..activeCount
  position: THREE.Vector3;
  speedMps: number;
  lateralOffsetTarget: number;
  laneTimer: number;
  skillPace: number;
  preset: VehicleDesignPreset;
  visual?: FrontierBuggyVisual;
  beaconMesh?: THREE.Mesh;
}

const REGION_LEG_LENGTH = 860; // Glowing Portal Gate every 860m!
const ELIMINATION_INTERVAL_SEC = 60.0; // Eliminate lowest-tally team every 1 minute!

export class TeamEliminationRaceDirector {
  public readonly aiGroup = new THREE.Group();

  public phase: RacePhase = 'TEAM_ELIMINATION';
  public regionIndex = 1; // 1 = Forest, 2 = Swamp, 3 = Desert, 4+ = Random!
  public eliminationTimer = ELIMINATION_INTERVAL_SEC;
  public totalDistanceRun = 0;
  public finishPlace: number | null = null;
  public playerEliminated = false;

  public teams: TeamInfo[] = [];
  public racers: RacerEntry[] = [];

  public onBeforeBiomeRebuild?: () => void;
  public onPortalTeleport?: (newBiome: BiomeId, regionNum: number, isFinalLeg: boolean) => void;
  public onTeamEliminated?: (teamName: string, wasPlayerTeam: boolean, enterSoloShowdown: boolean) => void;
  public onRaceFinished?: (playerRank: number) => void;

  private rng = mulberry32(202611);
  private rootNormalScratch = new THREE.Vector3();
  private nearbyRootsScratch: CrisscrossRootBridge[] = [];
  private lastPlayerZ = -4;

  constructor(
    private textures: ProceduralTextureLibrary,
    private world: TrailCorridorWorld,
    private playerVehicle: VehicleRigidBody
  ) {
    this.initTeamsAndRacers();
    this.setupPortalForCurrentRegion();
  }

  private initTeamsAndRacers(): void {
    const teamDefs = [
      { id: 'ALPHA', name: 'ALPHA SQUAD', colorHex: '#f5b041', isPlayerTeam: true },
      { id: 'VIPER', name: 'VIPER SQUAD', colorHex: '#52be80', isPlayerTeam: false },
      { id: 'PYRO', name: 'PYRO SQUAD', colorHex: '#ec7063', isPlayerTeam: false },
      { id: 'COBALT', name: 'COBALT SQUAD', colorHex: '#5dade2', isPlayerTeam: false },
      { id: 'IRON', name: 'IRON SQUAD', colorHex: '#dc7633', isPlayerTeam: false },
    ];

    this.teams = teamDefs.map((t) => ({
      ...t,
      eliminated: false,
      tallyScore: 0,
      memberRanks: [],
    }));

    const beaconGeo = new THREE.OctahedronGeometry(0.42, 0);

    // Build 10 Racers (2 per Team)
    let slotIdx = 0;
    for (const t of this.teams) {
      for (let m = 0; m < 2; m++) {
        const isPlayer = t.isPlayerTeam && m === 0;
        const isTeammate = t.isPlayerTeam && m === 1;
        const preset = VEHICLE_DESIGNS[(slotIdx + m) % VEHICLE_DESIGNS.length];

        const startLat = ((slotIdx % 5) - 2) * 5.8;
        const startZ = -4 - Math.floor(slotIdx / 5) * 6.5 - (isPlayer ? 0 : (this.rng() - 0.5) * 3.0);
        const startY = this.world.sampleHeightRaw(startLat, startZ).height + 0.85;

        let visual: FrontierBuggyVisual | undefined;
        let beaconMesh: THREE.Mesh | undefined;

        if (!isPlayer) {
          const dummyCreep = new THREE.Group();
          visual = new FrontierBuggyVisual(this.textures, dummyCreep);
          visual.setVehicleDesign(preset);

          const bMat = new THREE.MeshBasicMaterial({ color: new THREE.Color(t.colorHex) });
          beaconMesh = new THREE.Mesh(beaconGeo, bMat);
          beaconMesh.position.set(0, 2.35, 0);
          visual.chassisGroup.add(beaconMesh);

          this.aiGroup.add(visual.root);
        }

        const callsign = isPlayer
          ? 'YOU (ALPHA-1)'
          : isTeammate
          ? 'ALLY KADE (ALPHA-2)'
          : `${t.id}-${m + 1}`;

        this.racers.push({
          id: `${t.id}_${m + 1}`,
          callsign,
          teamId: t.id,
          teamName: t.name,
          teamColorHex: t.colorHex,
          isPlayer,
          isTeammate,
          eliminated: false,
          rank: slotIdx + 1,
          position: new THREE.Vector3(startLat, startY, startZ),
          speedMps: 16.0,
          lateralOffsetTarget: startLat,
          laneTimer: 1.5 + this.rng() * 3.0,
          skillPace: isTeammate ? 0.99 : 0.88 + this.rng() * 0.22,
          preset,
          visual,
          beaconMesh,
        });

        slotIdx++;
      }
    }
  }

  private setupPortalForCurrentRegion(): void {
    const isFinalLeg = this.phase === 'SOLO_SHOWDOWN';
    this.world.setPortalGateAt(-REGION_LEG_LENGTH, isFinalLeg);
  }

  public restartFullTournament(): void {
    this.phase = 'TEAM_ELIMINATION';
    this.regionIndex = 1;
    this.eliminationTimer = ELIMINATION_INTERVAL_SEC;
    this.totalDistanceRun = 0;
    this.finishPlace = null;
    this.playerEliminated = false;

    for (const t of this.teams) {
      t.eliminated = false;
      t.tallyScore = 0;
    }

    if (this.onBeforeBiomeRebuild) this.onBeforeBiomeRebuild();
    this.world.setBiomeAndRebuild('FOREST', -4);
    this.playerVehicle.resetAt(this.world, -4);
    this.playerVehicle.fuel = 100;
    this.lastPlayerZ = -4;

    this.racers.forEach((r, idx) => {
      r.eliminated = false;
      if (r.visual) r.visual.root.visible = true;
      const lat = ((idx % 5) - 2) * 6.0;
      const z = -4 - Math.floor(idx / 5) * 6.5;
      r.position.set(lat, this.world.sampleHeightRaw(lat, z).height + 0.85, z);
      r.speedMps = 14.0;
      r.lateralOffsetTarget = lat;
    });

    this.setupPortalForCurrentRegion();
  }

  /**
   * Picks the next Biome for the Portal Teleport!
   * Region 1 = FOREST -> Region 2 = SWAMP -> Region 3 = DESERT -> Region 4+ = Random Map!
   */
  private chooseNextBiome(): BiomeId {
    if (this.regionIndex === 1) return 'SWAMP';
    if (this.regionIndex === 2) return 'DESERT';
    if (this.regionIndex === 3) return 'DEADWOOD';
    if (this.regionIndex === 4) return 'SINGULARITY';
    if (this.regionIndex === 5) return 'GIANT_LOG';
    if (this.regionIndex === 6) return 'GLACIAL_ICE';
    if (this.regionIndex === 7) return 'CARNIVOROUS_BOG';
    if (this.regionIndex === 8) return 'UNDERGROUND_MARKET';
    const all: BiomeId[] = [
      'FOREST',
      'SWAMP',
      'DESERT',
      'DEADWOOD',
      'SINGULARITY',
      'GIANT_LOG',
      'GLACIAL_ICE',
      'CARNIVOROUS_BOG',
      'UNDERGROUND_MARKET',
    ];
    const candidates = all.filter((b) => b !== this.world.currentBiome);
    return candidates[Math.floor(this.rng() * candidates.length)];
  }

  /**
   * Teleports ALL surviving racers together into the new Biome (preserving relative race gaps!)
   */
  public triggerPortalWarpNow(): void {
    if (this.phase === 'FINISHED') return;

    // If this was the Grand Finish Gate in SOLO_SHOWDOWN, complete the Championship!
    if (this.phase === 'SOLO_SHOWDOWN' && this.world.activePortal?.isFinalFinish) {
      this.phase = 'FINISHED';
      const playerEntry = this.racers.find((r) => r.isPlayer)!;
      this.finishPlace = playerEntry.rank;
      if (this.onRaceFinished) this.onRaceFinished(playerEntry.rank);
      return;
    }

    const nextBiome = this.chooseNextBiome();
    this.regionIndex++;

    // Record relative Z offsets from player before teleporting so the whole pack spawns together!
    const playerZBefore = this.playerVehicle.position.z;
    const relZMap = new Map<string, number>();
    for (const r of this.racers) {
      if (!r.eliminated) {
        relZMap.set(r.id, clamp(r.position.z - playerZBefore, -42, 42));
      }
    }

    const arrivalBaseZ = -10;
    if (this.onBeforeBiomeRebuild) this.onBeforeBiomeRebuild();
    this.world.setBiomeAndRebuild(nextBiome, arrivalBaseZ);

    // Place Player at Arrival Portal preserving momentum + +30% Portal Fuel Bonus!
    const spec = this.world.getTrailSpec(arrivalBaseZ);
    const groundY = this.world.sampleHeightRaw(spec.centerX, arrivalBaseZ).height;
    const savedSpeed = clamp(Math.max(18, this.playerVehicle.forwardSpeedMps), 18, 38);

    this.playerVehicle.position.set(spec.centerX, groundY + 1.1, arrivalBaseZ);
    const yaw = Math.atan2(spec.slopeX, 1.0);
    this.playerVehicle.quaternion.setFromAxisAngle(new THREE.Vector3(0, 1, 0), yaw);
    const fwd = new THREE.Vector3(0, 0, -1).applyQuaternion(this.playerVehicle.quaternion);
    this.playerVehicle.linearVelocity.copy(fwd).multiplyScalar(savedSpeed);
    this.playerVehicle.angularVelocity.set(0, 0, 0);
    this.playerVehicle.fuel = Math.min(100, this.playerVehicle.fuel + 35);
    this.lastPlayerZ = arrivalBaseZ;

    // Teleport all surviving AI teammates & rivals into the SAME biome index!
    let spreadIdx = 0;
    for (const r of this.racers) {
      if (r.isPlayer || r.eliminated) continue;
      const relZ = relZMap.get(r.id) ?? 0;
      const rz = clamp(arrivalBaseZ + relZ, -58, -2);
      const rSpec = this.world.getTrailSpec(rz);
      const rx = rSpec.centerX + ((spreadIdx % 6) - 2.5) * 6.5;
      const ry = this.world.sampleHeightRaw(rx, rz).height + 0.85;
      r.position.set(rx, ry, rz);
      r.lateralOffsetTarget = rx - rSpec.centerX;
      spreadIdx++;
    }

    this.setupPortalForCurrentRegion();

    if (this.onPortalTeleport) {
      this.onPortalTeleport(nextBiome, this.regionIndex, this.phase === 'SOLO_SHOWDOWN');
    }
  }

  /**
   * Eliminates the surviving Team with the lowest combined Position Tally!
   * Once down to the Finalist Pack (<= 3 teams / ~5-6 racers), unlocks SOLO FREE-FOR-ALL SHOWDOWN!
   */
  public triggerTeamEliminationRound(): void {
    if (this.phase !== 'TEAM_ELIMINATION') return;

    const activeTeams = this.teams.filter((t) => !t.eliminated);
    if (activeTeams.length <= 2) {
      this.enterSoloShowdown();
      return;
    }

    // Sort ascending by tallyScore (lowest tally = worst combined positions!)
    // Tie-breaker: worst individual best rank
    activeTeams.sort((a, b) => {
      if (a.tallyScore !== b.tallyScore) return a.tallyScore - b.tallyScore;
      const worstA = Math.max(...a.memberRanks);
      const worstB = Math.max(...b.memberRanks);
      return worstB - worstA;
    });

    const eliminatedTeam = activeTeams[0];
    eliminatedTeam.eliminated = true;

    for (const r of this.racers) {
      if (r.teamId === eliminatedTeam.id) {
        r.eliminated = true;
        if (r.visual) r.visual.root.visible = false;
        if (r.isPlayer) this.playerEliminated = true;
      }
    }

    const remainingTeams = this.teams.filter((t) => !t.eliminated);
    // When down to final 2-3 teams (~4-6 cars -> "until the last 5"), transition to Solo Free-For-All Showdown!
    const survivingRacers = this.racers.filter((r) => !r.eliminated).length;
    const enteringShowdown = survivingRacers <= 6;

    if (enteringShowdown) {
      this.enterSoloShowdown();
    } else {
      this.eliminationTimer = ELIMINATION_INTERVAL_SEC;
    }

    if (this.onTeamEliminated) {
      this.onTeamEliminated(eliminatedTeam.name, eliminatedTeam.isPlayerTeam, enteringShowdown);
    }
  }

  private enterSoloShowdown(): void {
    this.phase = 'SOLO_SHOWDOWN';
    this.eliminationTimer = 0;
    // Upgrade current region's end portal into the Golden Championship Finish Gate!
    if (this.world.activePortal) {
      this.world.setPortalGateAt(this.world.activePortal.z, true);
    }
  }

  public update(dt: number, elapsedTime: number): void {
    // Track total cumulative expedition distance across all biome portals!
    const dzForward = Math.max(0, this.lastPlayerZ - this.playerVehicle.position.z);
    this.totalDistanceRun += dzForward;
    this.lastPlayerZ = this.playerVehicle.position.z;

    // Sync player entry
    const playerEntry = this.racers.find((r) => r.isPlayer)!;
    playerEntry.position.copy(this.playerVehicle.position);
    playerEntry.speedMps = Math.max(0, this.playerVehicle.forwardSpeedMps);

    // 1. Step 9 Physicalized AI Teammate & Rival Cars!
    this.world.getNearbyRootBridges(
      this.playerVehicle.position.z - 90,
      this.playerVehicle.position.z + 60,
      this.nearbyRootsScratch
    );

    const playerSpeed = clamp(this.playerVehicle.forwardSpeedMps, 18, 39);

    for (const r of this.racers) {
      if (r.isPlayer || r.eliminated) continue;

      r.laneTimer -= dt;
      const spec = this.world.getTrailSpec(r.position.z);
      if (r.laneTimer <= 0) {
        r.laneTimer = 2.0 + this.rng() * 3.2;
        if (spec.splitFactor > 0.35) {
          const branchSign = r.id.charCodeAt(0) % 2 === 0 ? -1 : 1;
          r.lateralOffsetTarget = branchSign * spec.splitOffset + (this.rng() - 0.5) * 9.0;
        } else {
          r.lateralOffsetTarget = (this.rng() - 0.5) * 2.0 * (spec.halfWidth * 0.68);
        }
      }

      // Competitive elastic rally pacing so all 5 teams battle closely within sight!
      const gapToPlayer = r.position.z - this.playerVehicle.position.z; // >0 means AI is behind player, <0 means AI is ahead!
      const rubberBand = clamp(gapToPlayer * 0.14, -4.5, 6.2);
      const baseCruise = lerp(25.5, 33.5, r.skillPace);
      const desiredSpeed = clamp(
        lerp(baseCruise, playerSpeed * r.skillPace, 0.55) + rubberBand,
        16.0,
        41.5
      );
      r.speedMps = lerp(r.speedMps, desiredSpeed, clamp(2.4 * dt, 0, 1));

      const targetX = spec.centerX + r.lateralOffsetTarget;
      const vx = clamp((targetX - r.position.x) * 1.65, -13.0, 13.0);
      const vz = -r.speedMps;

      r.position.x += vx * dt;
      r.position.z += vz * dt;

      // Sample ground + Static Crisscrossing Giant Root Bridges so AI cars also drive up onto giant roots!
      let groundY = this.world.sampleHeightRaw(r.position.x, r.position.z).height;
      const rootDeckY = this.world.sampleCrisscrossRootDeck(
        r.position.x,
        r.position.z,
        r.position.y,
        this.nearbyRootsScratch,
        this.rootNormalScratch
      );
      if (rootDeckY !== null && rootDeckY > groundY) {
        groundY = rootDeckY;
      }
      r.position.y = lerp(r.position.y, groundY + r.preset.wheelRadius + 0.36, clamp(14 * dt, 0, 1));

      // Gentle car-to-car bump deflection with player!
      if (!this.playerEliminated) {
        const dx = this.playerVehicle.position.x - r.position.x;
        const dz = this.playerVehicle.position.z - r.position.z;
        const dy = Math.abs(this.playerVehicle.position.y - r.position.y);
        const minSep = 2.35;
        const dSq = dx * dx + dz * dz;
        if (dy < 1.8 && dSq < minSep * minSep && dSq > 1e-4) {
          const d = Math.sqrt(dSq);
          const nx = dx / d;
          const nz = dz / d;
          const push = (minSep - d) * 0.5;
          this.playerVehicle.position.x += nx * push;
          this.playerVehicle.position.z += nz * push;
          r.position.x -= nx * push;
          r.position.z -= nz * push;
        }
      }

      // Update 3D AI Vehicle Visuals (chassis pose + spinning wheels!)
      if (r.visual) {
        const yaw = Math.atan2(-vx, -vz);
        r.visual.chassisGroup.position.copy(r.position);
        r.visual.chassisGroup.rotation.set(0, yaw, clamp(-vx * 0.018, -0.14, 0.14));
        if (r.beaconMesh) {
          r.beaconMesh.rotation.y = elapsedTime * 2.8;
        }
      }
    }

    // 2. Compute Live Race Positions (1..N sorted by furthest -Z!) & 5-Team Tally Scores!
    const activeRacers = this.racers.filter((r) => !r.eliminated);
    activeRacers.sort((a, b) => a.position.z - b.position.z); // more negative Z = further ahead = Rank 1!
    const totalActive = activeRacers.length;

    for (const t of this.teams) {
      t.tallyScore = 0;
      t.memberRanks = [];
    }

    activeRacers.forEach((r, idx) => {
      r.rank = idx + 1;
      const pts = totalActive - idx; // 1st gets highest points, last gets 1 pt!
      const team = this.teams.find((t) => t.id === r.teamId);
      if (team) {
        team.tallyScore += pts;
        team.memberRanks.push(r.rank);
      }
    });

    // 3. Step 1-Minute Team Elimination Countdown!
    if (this.phase === 'TEAM_ELIMINATION') {
      this.eliminationTimer -= dt;
      if (this.eliminationTimer <= 0) {
        this.triggerTeamEliminationRound();
      }
    }

    // 4. Check Region Portal Gate Crossing!
    if (this.world.activePortal && this.phase !== 'FINISHED') {
      if (this.playerVehicle.position.z <= this.world.activePortal.z + 3.5) {
        this.triggerPortalWarpNow();
      }
    }
  }

  public getPortalDistanceRemaining(): number {
    if (!this.world.activePortal) return 0;
    return Math.max(0, Math.round(this.playerVehicle.position.z - this.world.activePortal.z));
  }

  public getCurrentBiomeSpec() {
    return BIOME_SPECS[this.world.currentBiome];
  }
}
