# docs/ — GitHub Pages root

Settings → Pages → Source: **Deploy from a branch**, branch `main` (or this branch), folder **/docs**.

```
docs/
├── index.html          launcher (choose a panel)            https://sultanaladin.github.io/Frontier-/
├── celestial/          Celestial Panel + CelestialTextures/  …/Frontier-/celestial/
├── solidarc/           SolidArc Outliner·Viewport·Inspector  …/Frontier-/solidarc/
└── *.md                engine architecture notes
```

Deep links: `…/Frontier-/?panel=solidarc` redirects to the folder. Add a new tool = add a folder + a card in `index.html`.

`docs/solidarc/index.html` is a copy of `Editor/EditorTools/ParametricSketcher/Panel/index.html` — the source of truth is the tool folder; run `cp` (or `Scripts/publish-pages.sh`) after editing.
