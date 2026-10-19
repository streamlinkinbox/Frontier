# Complete streets: research and implementation notes

Research checked 19 October 2026. This is a procedural concept editor, not an approved civil-engineering drawing package.

## What the references suggest

1. **Separate the uses spatially.** NACTO's transit-corridor guidance discusses center-running transit and raised cycle tracks as distinct parts of the corridor. The generator therefore budgets a median/transit reservation separately from motor lanes, cycle tracks and buffers. [2](https://nacto.org/publication/urban-street-design-guide/streets/transit-corridor/)
2. **Keep crossings usable.** NACTO calls for cut-through crossings and protected refuge space; GDCI gives 1.8 m minimum and 2.4 m preferred refuge depth. The garden preset uses a 3.2 m central reservation, with planting interrupted by a clear crossing gap. The chosen dimensions and plant setbacks are editor defaults, not an assertion of standards compliance. [9](https://nacto.org/publication/urban-street-design-guide/intersection-design-elements/crosswalks-and-crossings/pedestrian-safety-islands/) [13](https://globaldesigningcities.org/publication/global-street-design-guide/designing-streets-people/designing-for-pedestrians/pedestrian-refuges/)
3. **Use the median as landscape, not spare asphalt.** NACTO's residential-boulevard guidance includes planting and street trees in a central median. The garden preset contains raised planted surfaces and deterministic tree/shrub placement; these positions are recalculated when the road changes. [8](https://nacto.org/publication/urban-street-design-guide/streets/residential-boulevard/)
4. **A continuous footway is not a zebra crossing.** Transport Scotland describes a continuous walking/cycling surface, low-speed side-road conditions and entrance ramps for vehicles. The supplied Infra CGI image illustrates that different junction concept. It is deliberately labelled reference-only rather than represented by an ordinary crossing painted over asphalt. [8](https://www.transport.gov.scot/publication/cycling-by-design-2026/junctions/)

## What is generated

- Garden boulevard: planted center, tree rhythm, low shrub buffers, red cycle tracks, clear pedestrian cut-throughs and tactile-color marks in the refuge.
- Protected cycle street: red cycle tracks outside raised planting buffers, distinct from motor lanes and the existing outer footways.
- Green tram boulevard: flush reserved grass bed, twin track pairs, separate motor lanes and cycling. No pedestrian waiting/refuge claim is made for the rail bed.
- Partitioned quad surfaces: no coincident asphalt sheet beneath the new reservation surfaces. Junction-mouth refinement preserves shared edges across conventional and landscaped approaches.
- A 28 m centerline-radius starter roundabout at (-230, 100), connected to the country road, with one-click framing. The ring is conventional, not the fully protected bicycle-roundabout illustration.

## Not generated / requires further design

Continuous-footway side-road junctions, protected cycle circulation around roundabouts, custom splitter-island layouts, tram turnouts/curved through-junction alignments, stops/platforms, overhead power, operational signals, vehicle swept paths, accessible-route certification, planting sight-line certification and drainage/hydrology simulation.

The source OBJ includes the road partitions and reservation surfaces. Trees, rail hardware, markings and surrounding buildings remain viewport previews. Surface material names and UVs are retained, but no MTL/texture bundle is supplied.

## Reference-image provenance

The in-app gallery uses images for design comparison, not as procedural surface textures or screenshots of generated output.

| Local image                                            | Provenance                                                                                                                                                                                                                                                 |
| ------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `public/references/landscape/planted-medians.jpg`      | User upload `10d3f8cafffb45f6a9a78db915f0ea68.jpg`                                                                                                                                                                                                         |
| `public/references/landscape/protected-roundabout.jpg` | User upload `3b750b3a2a401bb56f40b6cfe3fc1a2c.jpg`                                                                                                                                                                                                         |
| `public/references/landscape/urban-plan.jpg`           | User upload `d39d920912e2e54ada119d4daeaefe23.jpg`                                                                                                                                                                                                         |
| `public/references/landscape/continuous-footway.jpg`   | User upload `d9daafa5210d4126d301bb24ab9f94f1.jpg`; illustration bears Infra CGI attribution                                                                                                                                                               |
| `public/references/landscape/green-tram-reference.jpg` | Web-search thumbnail associated with [this green-track discussion](https://www.reddit.com/r/MelbourneTrains/comments/1jky4qf/how_come_none_of_our_tram_tracks_are_green_tracks/). Original photographer/license not established; reference thumbnail only. |

Image rights remain with the original owners. Do not assume these reference images are licensed production textures or reusable marketing assets.

## Two-city sample district

Eastbank now physically instantiates all implemented road and curb/drainage presets, plus railway/monorail examples. The central 480 m tram boulevard connects its rails across matching straight approaches by sampling the junction mesh height. A stationary tram is provided as a viewport scale reference; there is no vehicle simulation. The protected cycle roundabout and Dutch continuous-footway illustrations remain reference-only.
