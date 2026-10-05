# Frontier

## RoadWorks Editor

A standalone, static road-network and bridge editor is available at `app/roadworks.html` (no build step or backend required). It can be opened directly through raw.githack:

**[Open RoadWorks Editor](https://raw.githack.com/streamlinkinbox/Frontier/c83732630570d6d087ee8320ffc6aee7464b9e5c/app/roadworks.html)**

The editor keeps network topology separate from surface geometry: road endpoints attach to shared node IDs, junction surfaces are built from those shared nodes, and curved pavements and curbs are generated as parallel offsets along sampled alignments. It includes a connected-grid / riverside generator, editable road profiles, procedural bridge deck and support options, local save/undo, and JSON/SVG import/export.

### Quick controls

- **V** select/move · **R** draw/connect roads · **B** place a bridge · **M** measure · **F** fit network · **Esc** finish/cancel.
- Click a road or junction to inspect it. Drag junction nodes to reshape the network; endpoints within snap range merge into one shared junction.
- Select a bridge segment to edit deck form, underside clearance, span, support layout, and guardrails in the Inspector.
- Use **Generate** to build a fresh connected layout. Changes autosave in the current browser; **Export** downloads JSON or an SVG plan.

The existing Lobby and Wallet pages remain available in `app/index.html` and `app/wallet.html`; both link to RoadWorks Editor.
