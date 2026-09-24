# Frontier Editor — experimental HTML prototype

This folder contains the browser editor and its companion design studies:

- `index.html`: the React outliner/inspector and Construct menu (requires Vite).
- `icons.html`: standalone SVG icon gallery.
- `collection-icon-options.html`: standalone collection-icon design options.

```sh
# Run from this directory
npm ci
npm run dev -- --port 5173
npm run build
```

Visit `/`, `/icons.html` or `/collection-icon-options.html` on the dev server.
The build places all three pages and required SVGs in `dist/`.

The existing Construct creation bridge remains a separate native process. Start
it from the **repository root**, not this directory:

```sh
python3 Exhibits/Workbench/Construct/Build.py
python3 Exhibits/Workbench/Construct/Serve.py
```

Vite still proxies `/api/construct` to port 5191. Without that service, native
creation reports an error; the editor and standalone galleries still load.
Native C++ panel integration is documented in
[Native.md](../../Exhibits/Workbench/Construct/Native.md).

Browser scripts are in `scripts/`; run them from this directory. For example:

```sh
python3 scripts/serve-icons.py --port 5174
```

Keep approved SVG sources in `custom-icons/` and `ui-icons/`, and preserve vendor
notices in `vendor/`. Native runtime copies remain in `EngineContent/Icons/` at
the repository root. This move does not expand the engine's supported entities.
