"""Compare the engine's real J2000-catalogue rotation with Astropy/ERFA AltAz.

This is a model comparison, not observations: the CSV contains no proper motions, distances,
parallax or radial velocities. Refraction is disabled; the sample includes stars near both celestial poles.
"""

import csv
import io
import subprocess
import sys
from pathlib import Path

import numpy as np
from astropy import units as u
from astropy.coordinates import AltAz, EarthLocation, SkyCoord
from astropy.time import Time
from astropy.utils import iers

iers.conf.auto_download = False
iers.conf.auto_max_age = None


def main():
    probe = Path(sys.argv[1]).resolve()
    invalid = subprocess.run([str(probe), "not-a-date", "0", "0"], input="", text=True, capture_output=True)
    if invalid.returncode != 2:
        raise RuntimeError("probe must reject a malformed Julian date")
    source = Path("Exhibits/NightSky/Data/Hipparcos8.csv")
    with source.open(encoding="utf-8") as stream:
        rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
    indexes = set(np.linspace(0, len(rows) - 1, 500, dtype=int).tolist())
    indexes.update(sorted(range(len(rows)), key=lambda index: -abs(float(rows[index]["dec_deg"])))[:20])
    stars = [rows[index] for index in sorted(indexes)]
    text = io.StringIO()
    writer = csv.DictWriter(text, fieldnames=["id", "ra_deg", "dec_deg", "mag", "bv"], lineterminator="\n")
    writer.writeheader()
    writer.writerows(stars)
    ra = np.array([float(row["ra_deg"]) for row in stars])
    dec = np.array([float(row["dec_deg"]) for row in stars])
    coordinates = SkyCoord(ra=ra*u.deg, dec=dec*u.deg, frame="icrs")
    errors = []
    for utc in ("2026-01-02T03:00:00", "2026-06-15T12:00:00", "2026-10-18T13:00:00"):
        time = Time(utc, scale="utc")
        for name, lat, lon, height in (("Benoni", -26.2, 28.3, 1640), ("Equator", 0, 0, 0),
                                       ("Tromso", 69.65, 18.96, 0), ("Sydney", -33.87, 151.21, 30)):
            result = subprocess.run([str(probe), f"{time.ut1.jd:.16f}", str(lat), str(lon)], input=text.getvalue(),
                                    text=True, capture_output=True, check=True)
            actual = np.loadtxt(io.StringIO(result.stdout), ndmin=2)
            if actual.shape != (len(stars), 3):
                raise RuntimeError(f"decoder returned {actual.shape} for {len(stars)} stars")
            location = EarthLocation.from_geodetic(lon*u.deg, lat*u.deg, height*u.m)
            local = coordinates.transform_to(AltAz(obstime=time, location=location, pressure=0*u.hPa))
            alt = local.alt.to_value(u.rad)
            az = local.az.to_value(u.rad)
            expected = np.stack((np.cos(alt)*np.sin(az), np.cos(alt)*np.cos(az), np.sin(alt)), axis=1)
            dot = np.sum(actual*expected, axis=1)
            cross = np.linalg.norm(np.cross(actual, expected), axis=1)
            degrees = np.degrees(np.arctan2(cross, dot))
            errors.extend(degrees.tolist())
            print(f"{utc} {name:7s}: mean={np.mean(degrees):.8f} deg max={np.max(degrees):.8f} deg")
    values = np.array(errors)
    print(f"Astropy {__import__('astropy').__version__}; {len(values)} comparisons, {len(stars)} retained Hipparcos rows, "
          f"3 UTC dates, 4 sites; includes near-pole stars, zero pressure")
    print(f"direction angular difference mean={np.mean(values):.9f} deg "
          f"RMS={np.sqrt(np.mean(values**2)):.9f} deg max={np.max(values):.9f} deg")
    if not np.all(np.isfinite(values)) or np.max(values) >= 0.03:
        raise SystemExit("FAIL exceeds 0.03 degree scoped model-comparison tolerance")
    print("PASS scoped independent Astropy/ERFA coordinate comparison; no proper motion or observed-sky claim")


if __name__ == "__main__":
    main()
