# Frontier

Race-night lobby client (visual mock-up) — lobby browser, party room, wallet, and a
fully procedural vehicle garage.

## Running

No build step. Serve `app/` statically and open `car.html` (or `index.html`):

```bash
python3 -m http.server 8000 --bind 0.0.0.0 --directory app
# → http://localhost:8000/car.html
```

Three.js r170 is vendored in `app/vendor/` (import-mapped as `three`), so the app
runs fully offline. The Inter variable font lives in `app/fonts/`.

## Pages

| page          | what it is                                                     |
| ------------- | -------------------------------------------------------------- |
| `index.html`  | Lobby browser + party room (EOS-flavoured mock)                 |
| `wallet.html` | Wallet / top-up mock                                            |
| `car.html`    | **Garage** — procedural low-poly car studio (see below)         |

## Garage · `app/car.html` + `app/js/lowpoly-car.js`

The car you see is **generated at runtime from a seed + spec** — no imported meshes,
no textures, no baked models. `lowpoly-car.js` lofts a closed cross-section along a
list of X stations to build the hull and the greenhouse, then derives every hinged
panel (bonnet, boot/tailgate, doors with window pockets, drop tailgate on the ute)
from the *same* profile math so shut lines always match.

- 5 body styles: Coupe · Sedan · Hatch · GT · Ute (2/4 doors, tailgate vs boot lid)
- Seed-driven character: proportions, rims (5 spoke patterns × 5 finishes), paint,
  two-tone roof, livery stripes, lamps, mirrors, exhaust, drive side
- Articulated: bonnet, every door, boot/tailgate, and glass that rolls **down into
  the door pocket**; click a panel in the viewport (or its row, or keys 1-6) to cycle
  it, `O`/`C` open/close all, `R` new seed, `Space` turntable
- Live procedural dials (length, wheelbase, width, roof line, ride height, wheel Ø,
  facet density) rebuild the car in a few milliseconds
- Interior, engine bay, boot well, brakes, wipers, wing/splitter — all generated

### Offline preview renders (dev tool)

The sandbox has no browser, so `tools/preview.mjs` is a tiny software rasteriser that
renders the generated geometry straight to PNG for eyeballing:

```bash
node tools/preview.mjs --style gt --seed GT-77 --open all --windows 1 \
     --paint glacier --out .preview/gt
# writes side / front34 / rear34 / top / front PNGs + a spec dump
```

`.preview/` is git-ignored.
