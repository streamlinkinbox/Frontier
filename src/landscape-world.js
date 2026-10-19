import * as THREE from "three";
import { roadY } from "./geometry.js";
import { plantingStations, parkingBays } from "./landscape-layout.js";

export function buildLandscapeRoad(w, r) {
  const {
    e,
    a,
    dx,
    dz,
    nx,
    nz,
    length,
    layout: l,
    landscape: plan,
    samples,
  } = r;
  const at = (s, u = 0, h = 0) => [
    a.x + dx * s + nx * u,
    roadY(w.p, e, s / length) + h,
    a.z + dz * s + nz * u,
  ];
  const line = (s0, s1, u, width, mat = "white", height = 0.025) =>
    w.strip(
      at(s0, u, height),
      at(s1, u, height),
      width,
      0.009,
      mat,
      "markings",
    );
  const first = r.from * length,
    last = r.to * length;
  // Only motor lanes receive motor-vehicle arrows/dividers; never the reservation.
  if (e.markings) {
    for (const side of [-1, 1]) {
      for (let i = 1; i < e.lanes / 2; i++)
        for (let s = first + 1; s < last - 1; s += 6)
          line(
            s,
            Math.min(last, s + 2.5),
            side * (l.inner + i * e.laneWidth),
            0.14,
          );
      for (let i = 1; i < samples.length; i++) {
        const s0 = samples[i - 1] * length,
          s1 = samples[i] * length;
        line(s0, s1, side * (l.outer - 0.13), 0.12);
        if (l.median)
          line(
            s0,
            s1,
            side * (l.inner + 0.13),
            0.12,
            l.mode === "tram" ? "yellow" : "white",
          );
      }
      const s = (first + last) / 2;
      if (last - first > 14) {
        for (let lane = 0; lane < e.lanes / 2; lane++)
          w.arrow(
            at(s, side * (l.inner + (lane + 0.5) * e.laneWidth), 0.045),
            -side * dx,
            -side * dz,
          );
        if (l.cycle)
          w.bicycle(
            at(s, side * (l.outer + l.parking + l.buffer + l.cycle / 2), 0.045),
            -side * dx,
            -side * dz,
          );
      }
    }
    if (!l.median)
      for (let s = first + 1; s < last - 1; s += 6)
        line(s, Math.min(last, s + 2.5), 0, 0.14);
    for (const c of plan.crossings) {
      for (const band of l.bands.filter((b) =>
        ["traffic", "cycle", "parking"].includes(b.kind),
      ))
        for (let u = band.lo + 0.35; u < band.hi - 0.2; u += 1.05)
          line(c - 1.25, c + 1.25, u, 0.48);
      if (l.mode === "garden")
        for (const u of [-l.inner + 0.32, l.inner - 0.32])
          line(c - 1.2, c + 1.2, u, 0.42, "yellow", 0.035);
      const dir = c < (first + last) / 2 ? 1 : -1,
        s = c + dir * 2.7;
      w.strip(
        at(s, -dir * (l.inner + 0.2), 0.035),
        at(s, -dir * (l.outer - 0.2), 0.035),
        0.28,
        0.012,
        "white",
        "markings",
      );
    }
  }
  for (const [i, bay] of parkingBays(r).entries()) {
    if (e.markings) {
      for (const s of [bay.s0, bay.s1])
        w.strip(
          at(s, bay.u - bay.width / 2, 0.03),
          at(s, bay.u + bay.width / 2, 0.03),
          0.09,
          0.008,
          "white",
          "markings",
        );
      line(bay.s0, bay.s1, bay.u + bay.side * (bay.width / 2 - 0.06), 0.09);
    }
    if (i % 3 === 1) continue;
    const s = (bay.s0 + bay.s1) / 2,
      angle = Math.atan2(dx, dz),
      p = at(s, bay.u, 0.6);
    w.box(...p, 1.75, 0.65, 4.5, i % 2 ? "parkCar" : "roof", "details", angle);
    const roof = at(s - 0.15, bay.u, 1.1);
    w.box(...roof, 1.52, 0.45, 2.55, "tramGlass", "details", angle);
    for (const along of [-1.4, 1.4])
      for (const side of [-1, 1]) {
        const wheel = new THREE.CylinderGeometry(0.3, 0.3, 0.16, 8);
        wheel.rotateZ(Math.PI / 2);
        wheel.rotateY(angle);
        wheel.translate(...at(s + along, bay.u + side * 0.9, 0.3));
        w.batch(wheel, "tramGlass", "details");
      }
  }
  if (l.mode === "tram") {
    for (let i = 1; i < samples.length; i++)
      for (const center of [-1.65, 1.65])
        for (const gauge of [-0.7175, 0.7175]) {
          w.strip(
            at(samples[i - 1] * length, center + gauge, 0.003),
            at(samples[i] * length, center + gauge, 0.003),
            0.075,
            0.012,
            "rail",
            "rail",
          );
        }
  }
  for (const plant of plantingStations(l, plan)) {
    const p = at(
      plant.s,
      plant.u,
      l.mode === "tram" && Math.abs(plant.u) < l.inner ? 0 : 0.15,
    );
    if (plant.kind === "tree") w.tree(p[0], p[1], p[2], plant.size);
    else {
      const crown = new THREE.IcosahedronGeometry(plant.size, 1);
      crown.scale(1, 0.8, 1);
      crown.translate(p[0], p[1] + plant.size * 0.8, p[2]);
      w.batch(crown, "tree2", "vegetation");
    }
  }
}

// A stationary two-car tram makes the reserved tracks readable at city scale.
export function buildSampleTram(w, r) {
  if (
    !r.e.tramVehicle ||
    r.e.landscape !== "tram" ||
    (r.to - r.from) * r.length < 32
  )
    return;
  const mid = ((r.from + r.to) * r.length) / 2,
    angle = Math.atan2(r.dx, r.dz);
  const box = (s, u, h, x, y, z, mat) => {
    const p = [
      r.a.x + r.dx * s + r.nx * u,
      roadY(w.p, r.e, s / r.length) + h,
      r.a.z + r.dz * s + r.nz * u,
    ];
    w.box(...p, x, y, z, mat, "rail", angle);
  };
  for (const offset of [-4.6, 4.6]) {
    const s = mid + offset;
    box(s, 1.65, 1.45, 2.4, 2.5, 8.4, "tramBody");
    box(s, 1.65, 2.82, 2.3, 0.18, 8.1, "roof");
    for (const end of [-2.65, 2.65])
      box(s + end, 1.65, 0.25, 2.05, 0.4, 1.3, "rail");
    for (const side of [-1, 1])
      for (const window of [-2.7, -0.9, 0.9, 2.7])
        box(s + window, 1.65 + side * 1.206, 1.95, 0.025, 1, 1.5, "tramGlass");
    for (const end of [-1, 1])
      box(s + end * 4.21, 1.65, 2, 1.9, 0.9, 0.025, "tramGlass");
  }
  box(mid, 1.65, 1.4, 1.9, 2, 0.8, "rail");
}
