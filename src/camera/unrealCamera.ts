import * as THREE from 'three';
import { clamp, lerp } from '../math/noise';
import { TrailCorridorWorld } from '../world/trailCorridor';

export class UnrealObserverCamera {
  public readonly position = new THREE.Vector3(0, 6.5, 6.0);
  public readonly velocity = new THREE.Vector3();

  /** Yaw around +Y (0 = looking straight down -Z along the course) */
  public yaw = 0;
  /** Pitch up/down in radians */
  public pitch = -0.14;

  /** Unreal viewport base flight speed in m/s (Mouse Wheel adjusts 6..260 m/s) */
  public baseSpeedMps = 44.0;
  public isDraggingLook = false;

  private activePointerId: number | null = null;
  private lastPointerX = 0;
  private lastPointerY = 0;

  private _euler = new THREE.Euler(0, 0, 0, 'YXZ');
  private _quat = new THREE.Quaternion();
  private _forward = new THREE.Vector3();
  private _right = new THREE.Vector3();
  private _targetVel = new THREE.Vector3();

  public onSpeedChanged?: (newSpeedMps: number) => void;

  constructor(canvas: HTMLCanvasElement, private isEnabledCheck: () => boolean) {
    // Prevent right-click context menu on viewport so Unreal RMB+WASD works cleanly!
    canvas.addEventListener('contextmenu', (e) => {
      e.preventDefault();
    });

    canvas.addEventListener('pointerdown', (e) => {
      if (!this.isEnabledCheck()) return;
      if (e.button === 0 || e.button === 2 || e.button === 1) {
        this.isDraggingLook = true;
        this.activePointerId = e.pointerId;
        this.lastPointerX = e.clientX;
        this.lastPointerY = e.clientY;
        try {
          canvas.setPointerCapture(e.pointerId);
        } catch {
          // Ignore in headless/synthetic environments
        }
      }
    });

    window.addEventListener('pointermove', (e) => {
      if (!this.isEnabledCheck() || !this.isDraggingLook) return;
      if (this.activePointerId !== null && e.pointerId !== this.activePointerId) return;

      const dx = e.clientX - this.lastPointerX;
      const dy = e.clientY - this.lastPointerY;
      this.lastPointerX = e.clientX;
      this.lastPointerY = e.clientY;

      const sensitivity = 0.0032;
      this.yaw -= dx * sensitivity;
      this.pitch = clamp(this.pitch - dy * sensitivity, -1.48, 1.48);
    });

    const endDrag = (e: PointerEvent) => {
      if (this.activePointerId === null || e.pointerId === this.activePointerId) {
        this.isDraggingLook = false;
        this.activePointerId = null;
      }
    };
    window.addEventListener('pointerup', endDrag);
    window.addEventListener('pointercancel', endDrag);

    // Unreal Editor Scroll Wheel -> Adjust Camera Flight Speed!
    canvas.addEventListener(
      'wheel',
      (e) => {
        if (!this.isEnabledCheck()) return;
        e.preventDefault();
        const factor = e.deltaY < 0 ? 1.18 : 1 / 1.18;
        this.baseSpeedMps = clamp(this.baseSpeedMps * factor, 6.0, 260.0);
        if (this.onSpeedChanged) this.onSpeedChanged(this.baseSpeedMps);
      },
      { passive: false }
    );
  }

  public placeAtCourseZ(world: TrailCorridorWorld, z: number, heightAboveGround = 7.5): void {
    const spec = world.getTrailSpec(z);
    const groundY = world.sampleHeightRaw(spec.centerX, z).height;
    this.position.set(spec.centerX, groundY + heightAboveGround, z);
    this.velocity.set(0, 0, 0);
    // Point camera along the natural curve of the trail ahead
    this.yaw = Math.atan2(spec.slopeX, 1.0);
    this.pitch = -0.12;
  }

  public getForwardVector(out = new THREE.Vector3()): THREE.Vector3 {
    this._euler.set(this.pitch, this.yaw, 0, 'YXZ');
    this._quat.setFromEuler(this._euler);
    return out.set(0, 0, -1).applyQuaternion(this._quat).normalize();
  }

  public step(
    dt: number,
    keys: ReadonlySet<string>,
    camera: THREE.PerspectiveCamera,
    world: TrailCorridorWorld
  ): void {
    // Optional Arrow-Key Look (convenient alongside mouse RMB/LMB drag)
    const keyTurnSpeed = 1.65;
    if (keys.has('ArrowLeft')) this.yaw += keyTurnSpeed * dt;
    if (keys.has('ArrowRight')) this.yaw -= keyTurnSpeed * dt;
    if (keys.has('ArrowUp')) this.pitch = clamp(this.pitch + keyTurnSpeed * 0.8 * dt, -1.48, 1.48);
    if (keys.has('ArrowDown')) this.pitch = clamp(this.pitch - keyTurnSpeed * 0.8 * dt, -1.48, 1.48);

    this._euler.set(this.pitch, this.yaw, 0, 'YXZ');
    this._quat.setFromEuler(this._euler);

    this._forward.set(0, 0, -1).applyQuaternion(this._quat).normalize();
    this._right.set(1, 0, 0).applyQuaternion(this._quat);
    this._right.y = 0;
    if (this._right.lengthSq() > 1e-5) this._right.normalize();

    const moveFwd = (keys.has('KeyW') ? 1 : 0) - (keys.has('KeyS') ? 1 : 0);
    const moveRight = (keys.has('KeyD') ? 1 : 0) - (keys.has('KeyA') ? 1 : 0);
    const moveUp =
      (keys.has('KeyE') || keys.has('Space') ? 1 : 0) -
      (keys.has('KeyQ') || keys.has('KeyC') ? 1 : 0);

    const sprintMult = keys.has('ShiftLeft') || keys.has('ShiftRight') ? 3.2 : 1.0;
    const speed = this.baseSpeedMps * sprintMult;

    this._targetVel.set(0, 0, 0);
    this._targetVel.addScaledVector(this._forward, moveFwd);
    this._targetVel.addScaledVector(this._right, moveRight);
    this._targetVel.y += moveUp;

    if (this._targetVel.lengthSq() > 1.0) {
      this._targetVel.normalize();
    }
    this._targetVel.multiplyScalar(speed);

    // Responsive Unreal viewport damping
    const accelBlend = clamp(14.0 * dt, 0, 1);
    this.velocity.x = lerp(this.velocity.x, this._targetVel.x, accelBlend);
    this.velocity.y = lerp(this.velocity.y, this._targetVel.y, accelBlend);
    this.velocity.z = lerp(this.velocity.z, this._targetVel.z, accelBlend);

    this.position.addScaledVector(this.velocity, dt);

    // Keep within corridor bounds & gently above terrain skin so you never clip underground
    const spec = world.getTrailSpec(this.position.z);
    this.position.x = clamp(this.position.x, spec.centerX - 150, spec.centerX + 150);
    const minGroundY = world.sampleHeightRaw(this.position.x, this.position.z).height + 0.65;
    if (this.position.y < minGroundY) {
      this.position.y = minGroundY;
      if (this.velocity.y < 0) this.velocity.y = 0;
    }
    this.position.y = Math.min(this.position.y, 240);

    camera.position.copy(this.position);
    camera.quaternion.copy(this._quat);
  }
}
