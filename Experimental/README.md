# Experimental browser prototypes

These browser design studies are separate from the native C++ editor.

| Prototype | Entry point |
| --- | --- |
| Frontier Editor — outliner, inspector and sliding Construct menu | [FrontierEditor/index.html](FrontierEditor/index.html) |
| SVG icon gallery | [FrontierEditor/icons.html](FrontierEditor/icons.html) |
| Collection-icon options | [FrontierEditor/collection-icon-options.html](FrontierEditor/collection-icon-options.html) |
| Interactive Vehicle HMI Configurator — React + Three.js study for drag-and-drop exterior paint/wheel material application with 3D spherical ripple waves and X-Ray / Ghost-Wireframe scan mode | [interactive-vehicle-hmi-configurator/index.html](interactive-vehicle-hmi-configurator/index.html) |
| WebGPU distance-field GI · surface cards · ReSTIR GI in the Frontier Editor UI — proving grounds and training arena, smoke + fire editor, burnouts, soft tyres with pressure, bursts and rim sparks, the TabletPanel HMI in a full, resizable cockpit with mirrors, live HMI Garage material configurator + X-Ray / Ghost-Wireframe scan view updating the real 3D car & GI, fracture editor with rigid-body debris, derez VFX | [IlluminationHost/index.html](IlluminationHost/index.html) |

The editor's JSX/CSS, SVG assets, package files, browser scripts and vendor
provenance moved together into `FrontierEditor/`, preserving relative links.
The two standalone galleries can also be opened directly in a browser.

From the repository root:

```sh
npm --prefix Experimental/FrontierEditor ci
npm --prefix Experimental/FrontierEditor run dev -- --port 5173
npm --prefix Experimental/FrontierEditor run build
```

Production output goes to `Experimental/FrontierEditor/dist/` and includes all
three pages plus their assets. No npm package or web entry point remains at the
repository root.

Native C++ sources and runtime assets have **not** moved. Generated native
proof/report HTML stays in `Exhibits/Gallery/` with its associated evidence.
The native icon import/bake tools now use the approved SVG sources here.

`IlluminationHost/` is the WebGPU global-illumination exhibit imported from
`SultanAladin/Slate` (branch `arena/c6cba543-slate`, commit `e7c4ea47`) and
developed further here — the Frontier Editor shell, the proving grounds, smoke and fire realism, burnouts and
breakable props; serve the folder with any static server (for example `python3 -m http.server`) and see
[IlluminationHost/README.md](IlluminationHost/README.md).
