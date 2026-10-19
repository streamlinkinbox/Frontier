# Transport additions and repair contract

## Parking research → generator choices

Sources consulted:

- [NACTO: Designing Protected Bike Lanes](https://nacto.org/publication/urban-bikeway-design-guide/designing-bikeways-for-all-ages-and-abilities/protected-bike-lanes/designing-protected-bike-lanes/): separate the rideable width from the parking-door buffer; at least 0.9 m buffer beside parking, and approximately 2–2.1 m minimum one-way protected riding width.
- [NACTO: Separating Protected Bike Lanes](https://nacto.org/publication/urban-bikeway-design-guide/designing-bikeways-for-all-ages-and-abilities/protected-bike-lanes/separating-protected-bike-lanes/): parking may provide protection, but door swing must not consume the rideable area. Avoid obstructions in the door zone.
- [NACTO protected-bike-lane guidance](https://nacto.org/latest/urban-bikeway-design-guide-protected-bike-lanes/): approximately 2.1 m is a typical parallel parking-lane width.
- [NACTO: Curbside Activity](https://nacto.org/publication/urban-bikeway-design-guide/designing-bikeways-for-all-ages-and-abilities/curbside-activity/): accessible parking requires additional transfer/access space. These ordinary bays are **not accessible parking spaces**.

The concept preset uses 2.2 m parking, 6.4 m bay length, 0.9 m unobstructed door space, 2 m cycling and two 3.1 m traffic lanes: 16.4 m between the outer cycle-track edges. Sidewalks are additional. The 2.2 m/6.4 m values and 10 m setback from trimmed junction mouths are explicit prototype choices, not universal regulations. Parking bays do not extend through crossing/ramp areas. Parked cars are static low-poly references. The door zone is flat and plant-free. The separate road-surface bands, including parking, are part of the source quad mesh; cars and bay paint are preview geometry.

## Railway crossing research → generator choices

- [FHWA Highway-Rail Crossing Handbook, Appendix B](https://highways.dot.gov/safety/hsip/xings/highway-rail-crossing-handbook-third-edition/b-appendix-components-highway-rail): prefer crossings as close to a right angle as practicable.
- [ITE crossing design guidance](https://toolkits.ite.org/gradecrossing/sec04c.htm): prefer straight approaches, avoid nearby junction conflicts, and avoid road/rail curves where practical.
- [MUTCD 11th edition, Part 8](https://mutcd.fhwa.dot.gov/pdfs/11th_Edition/part8.pdf): stop lines must not be closer than 15 ft (4.57 m) to the nearest rail; placement relative to gates/signals also matters.
- [WSDOT Design Manual, chapter 1350](https://wsdot.wa.gov/publications/manuals/fulltext/m22-01/1350.pdf): avoid roadway humps capable of trapping low-clearance vehicles.

The two built examples cross at approximately 79.7° and 85.3°. Their rails transition to the road surface, rather than carrying ballast and sleepers across traffic. Crossing deck faces replace corresponding asphalt faces in the source mesh; they are not another coplanar road slab. Stop lines are 4.8 m beyond the projected nearest rail; gates are 2.1 m beyond it. Opposing half-road barriers, crossbucks and red signal heads are **static illustration only**.

The editor deliberately rejects new crossings under 60°, within 18 m of a road endpoint, or through raised sidewalks/landscaped medians. These are conservative prototype constraints, not a full railway design standard. Urban footway crossings need a dedicated curb/ramp/tactile design and are not silently accepted. Sight distance, stopping distance, vehicle clearance, warning timings, queuing, signalling/interlocking, utility interfaces and local South African requirements still require professional design. This tool does not certify railway safety.

## Independent rail departures

`Rail exits ↗` generates a quarter-circle and tangent run from an open-ended tram street. Ground railway uses two track pairs with 1.435 m gauge and 3.3 m spacing. Railheads share the street endpoint's height and tangent. Radius is adjustable from 30–200 m; the UI's tangent run is 100 m. Elevated monorail includes a straight lead over the street, then leaves it on the chosen side. Road-spanning portals keep columns outside the carriageway/footway; off-road piers connect to the beam through caps.

These are independent departures, **not turnouts or switches through live tracks**. The stored attachment follows endpoint moves. Dedicated arbitrary rail-node manipulation, cant/superelevation, station platforms, network routing and moving vehicles remain outside this prototype.

Swept rail, deck and arch beams use shared cross-sections rather than a chain of overlapping boxes. Geometry tests check noncollapsed faces, outward orientation, finite coordinates/UVs and the street/rail endpoint match. Hardware is still viewport geometry, not part of the quad OBJ.

## Bridges

The South bowstring and North girder routes include connected surface approaches. Both are grade-separated from their underpass roads and exceed 5.5 m clearance beneath the modeled girders in the fixture. Deck slabs support the complete road and footway width; pedestrian rails sit outside the sidewalk, not in it. Arch ribs, hangers, girders and piers are visible. Piers near an underlying road are omitted.

These examples do not claim structural design, load capacity, drainage detailing, abutment/geotechnical design or universal clearance validation. Arbitrary user bridges still require inspection. Bridge details are viewport-only; the underlying road and streetscape surfaces remain exportable quads.

## Saved-map repair and validation

The reported screenshots show a 142-road saved map with topology warnings, not the earlier 136-road regression fixture. Its exact JSON is not available in the test browser, so the precise edit that produced those screenshots has **not** been proven.

Confirmed gaps were the editor's omission of short-edge audit warnings from edit rejection and marking placement using original segment length instead of usable junction-trimmed length. These are corrected. A regression fixture deliberately inserts a tiny tagged Eastbank stub and verifies repair, backup, preservation and reload.

On first transport upgrade:

1. Save a full browser-local JSON backup before changing the graph. Storage failure cancels the update.
2. Audit the existing graph. Healthy samples are extended. An invalid generated district is rebuilt at its original origin using its original connector anchor.
3. Replace only `district: eastbank` roads and four explicitly named generated rail corridors. Preserve Westhaven, untagged custom roads and terrain. Custom rail attachments to removed sample roads become independent stored alignments.
4. Validate the complete candidate before assignment. If remaining/custom geometry conflicts, leave the project unchanged and report the failure.
5. Keep an undo snapshot and display a repair/update notice. **Backup ↓** downloads the saved JSON.

The manual **Repair Eastbank** action explains that edits to tagged generated roads will be replaced and requires confirmation. Its backup is `frontier-before-eastbank-repair-v1`; the initial upgrade backup is `frontier-before-transport-v1`. Backups are browser-local, so download JSON before clearing browser storage. The editor's existing import restores a downloaded project.

Verification commands:

```sh
npm test
npm run build
npm run test:transport
TRANSPORT_PHASE=repair npm run test:transport
npm run test:second-city
```
