# D-DAY · Race the Tide (Operation Frontier)

A low-poly 3D browser game built with Three.js — no build step, fully static.

You drive a low-poly sedan off the beach while the ocean tide rises behind you.
The only way out is the **giant wall** inland — but its sentry bunkers open fire
on any vehicle in sight.

**Features**
- Clean low-poly terrain: beach, dirt roads, huge earth mounds, carved trenches, craters
- Giant wall with gate towers + sentry guns in concrete defensive bunkers (they shoot vehicles)
- Static tanks (some knocked out), landing craft, Czech-hedgehog barricades, wooden road chicanes, dragon's teeth
- Anti-tank mines and exploding AP mines (with minefield warning signs)
- Sandbag parapets, barbed-wire rows with road gaps
- Rising ocean tide — get caught below the waterline and you drown
- Procedural audio (explosions, gunfire), particle explosions, camera shake

**Controls:** `W A S D` / arrows to drive, `SPACE` handbrake, `R` restart.

**Run it:** serve this folder with any static server, e.g.

```bash
python3 -m http.server 8000 --directory dday
```

then open http://localhost:8000
