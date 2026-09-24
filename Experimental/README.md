# Experimental browser prototypes

These browser design studies are separate from the native C++ editor.

| Prototype | Entry point |
| --- | --- |
| Frontier Editor — outliner, inspector and sliding Construct menu | [FrontierEditor/index.html](FrontierEditor/index.html) |
| SVG icon gallery | [FrontierEditor/icons.html](FrontierEditor/icons.html) |
| Collection-icon options | [FrontierEditor/collection-icon-options.html](FrontierEditor/collection-icon-options.html) |

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
