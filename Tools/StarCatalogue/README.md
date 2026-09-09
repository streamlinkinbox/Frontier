# Star Catalogue

Slate renders stars from real positions, not a procedural hash. Two files matter here.

## `BrightStars.csv` — the committed subset

178 stars, every one a real catalogue entry: right ascension and declination at J2000, visual magnitude, and
B−V colour index. This is roughly everything to magnitude 3.0 plus the fainter members of the patterns people
actually recognise.

🔴 **It is not the whole naked-eye sky.** That is ~9 100 stars. What this subset guarantees is that the
constellations a viewer can name are in their true positions, because every star in Orion, the Plough, Cassiopeia
and the Southern Cross is brighter than magnitude 3.

Verified against known values rather than trusted: Orion's belt spans 2.74° against a real 2.70°, the Big Dipper
spans 25.71° against 25.6°, and Polaris sits at declination 89.26°.

## `ConvertHygCatalogue.py` — the full catalogue

To render every naked-eye star, fetch the HYG database and convert it:

```
curl -L -o hygdata_v41.csv \
    https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/CURRENT/hygdata_v41.csv
python3 Tools/StarCatalogue/ConvertHygCatalogue.py hygdata_v41.csv \
    EngineContent/StarCatalogue/BrightStars.bin
```

That produces ~9 100 stars in ~256 KB. The engine reads whichever catalogue is present, so this is a drop-in
upgrade with no code change.

The converter is committed rather than only its output. A binary blob in a source tree that no one can regenerate
is a liability: this way the asset is always reproducible from a documented source.

## Why a catalogue rather than a hash

The procedural field it replaces had three defects a hash cannot fix:

- **No constellations.** The sky is recognisable *because* of Orion and the Plough. Random points never can be.
- **Invented magnitudes**, so the brightness distribution was wrong.
- **No celestial pole**, so it could not turn correctly with latitude.

## Why not a cube map

Stars are point sources. A cube map at any resolution that resolves one is enormous, and it blurs under
filtering. 178 stars is 5 KB; 9 100 is 256 KB. Both are smaller and sharper than any texture that could hold them.

## Colour

B−V is a temperature measurement — negative is hot and blue, positive is cool and red. The converter goes
B−V → temperature (Ballesteros) → blackbody RGB, so Rigel comes out blue and Betelgeuse red *for the reason they
actually are*, rather than by assignment. Checked: Rigel's blue/red ratio is 1.29, Betelgeuse's is 0.52.

## Licence

HYG is CC BY-SA 4.0 (astronexus/HYG-Database), aggregating Hipparcos, the Yale Bright Star Catalogue and Gliese.
