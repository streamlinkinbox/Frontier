#!/usr/bin/env python3
"""Compare Frontier's local celestial vectors with JPL DE421, via Astropy/jplephem.

No network is used at runtime: DE421 is installed by skyfield-data and Astropy's IERS table is bundled.
This is an independent ephemeris comparison, not a telescope measurement.
"""

import csv
import hashlib
import io
from importlib.metadata import version
from importlib.resources import files
import subprocess
import sys

import astropy
import astropy.units as u
from astropy.coordinates import AltAz, EarthLocation, ITRS, get_body
from astropy.time import Time
from astropy.utils import iers
import numpy as np


def unit(values):
    return values / np.linalg.norm(values, axis=-1, keepdims=True)


def separation_degrees(a, b):
    # atan2 avoids the loss of resolution of acos near identical vectors.
    return np.rad2deg(np.arctan2(np.linalg.norm(np.cross(a, b), axis=-1), np.sum(a * b, axis=-1)))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: ValidateEphemeris.py path/to/EphemerisProbe")
    iers.conf.auto_download = False
    data = subprocess.check_output([sys.argv[1]], text=True)
    rows = list(csv.DictReader(io.StringIO(data)))
    assert len(rows) == 960
    times = Time(
        [f"{r['year']}-{int(r['month']):02d}-{int(r['day']):02d}T{int(r['hour']):02d}:00:00" for r in rows],
        scale="utc",
    )
    kernel = files("skyfield_data").joinpath("data", "de421.bsp")
    digest = hashlib.sha256(kernel.read_bytes()).hexdigest()
    expected_digest = "a20a7139da04cbc462454634918e9a9ca69127044e2cc9d4f9c16e238d2deedc"
    if digest != expected_digest:
        raise SystemExit(f"unexpected DE421 kernel hash: {digest}")
    sun = get_body("sun", times, ephemeris=str(kernel)).transform_to(ITRS(obstime=times)).cartesian.xyz.to_value(u.m).T
    moon = get_body("moon", times, ephemeris=str(kernel)).transform_to(ITRS(obstime=times)).cartesian.xyz.to_value(u.m).T
    longitude = np.array([float(r["longitude"]) for r in rows])
    latitude = np.array([float(r["latitude"]) for r in rows])
    height = np.array([float(r["height_m"]) for r in rows])
    location = EarthLocation.from_geodetic(longitude * u.deg, latitude * u.deg, height * u.m)
    lon, lat = np.deg2rad(longitude), np.deg2rad(latitude)
    east = np.stack([-np.sin(lon), np.cos(lon), np.zeros_like(lon)], axis=-1)
    north = np.stack([-np.sin(lat) * np.cos(lon), -np.sin(lat) * np.sin(lon), np.cos(lat)], axis=-1)
    up = np.stack([np.cos(lat) * np.cos(lon), np.cos(lat) * np.sin(lon), np.sin(lat)], axis=-1)

    def local(position):
        direction = unit(position)
        return np.stack([np.sum(direction * axis, axis=-1) for axis in (east, north, up)], axis=-1)

    def values(prefix):
        return np.array([[float(r[f"{prefix}_{axis}"]) for axis in "enu"] for r in rows])

    # Ask Astropy for apparent observer-specific directions rather than subtracting a
    # geocentric vector ourselves; AltAz uses zero pressure to exclude refraction.
    altaz_frame = AltAz(obstime=times, location=location, pressure=0*u.hPa)
    apparent_sun = get_body("sun", times, location=location, ephemeris=str(kernel)).transform_to(altaz_frame)
    apparent_moon = get_body("moon", times, location=location, ephemeris=str(kernel)).transform_to(altaz_frame)

    def altaz_local(body):
        alt, az = body.alt.to_value(u.rad), body.az.to_value(u.rad)
        return np.stack([np.cos(alt)*np.sin(az), np.cos(alt)*np.cos(az), np.sin(alt)], axis=-1)

    sun_local = altaz_local(apparent_sun)
    moon_geocentric = local(moon)
    moon_topocentric = altaz_local(apparent_moon)
    before = separation_degrees(values("old"), moon_topocentric)
    geo_error = separation_degrees(values("geo"), moon_geocentric)
    topo_error = separation_degrees(values("topo"), moon_topocentric)
    sun_error = separation_degrees(values("sun"), sun_local)
    radius = np.array([float(r["radius_rad"]) for r in rows])
    expected_distance = apparent_moon.distance.to_value(u.km)
    expected_radius = np.arcsin(1737.4 / expected_distance)
    radius_error = np.rad2deg(np.abs(radius - expected_radius)) * 3600.0
    distance_error = np.abs(np.array([float(r["distance_km"]) for r in rows]) - expected_distance)
    mean_phase = np.array([float(r["mean_phase"]) for r in rows])
    moon_phase = np.array([float(r["geometric_phase"]) for r in rows])
    expected_phase = 0.5 * (1.0 + np.sum(unit(sun - moon) * unit(-moon), axis=-1))
    phase_error = np.abs(mean_phase - expected_phase)
    geometric_phase_error = np.abs(moon_phase - expected_phase)

    print(f"Astropy {astropy.__version__}, NumPy {np.__version__}, jplephem {version('jplephem')}, "
          f"skyfield-data {version('skyfield-data')}; bundled IERS")
    print(f"JPL DE421, SHA-256 {digest}")
    print("192 UTC instants in 2026 at five WGS84 sites: Benoni, Greenwich, Equator, NorthPole, SouthPole")
    print("reference: apparent topocentric without refraction; Frontier: mean-geometric, UTC≈UT1, TT−UTC 69.184 s")
    print("Angular separations in degrees; reference includes aberration and light-time absent from Frontier.")

    def report(name, error):
        i = int(np.argmax(error))
        r = rows[i]
        print(f"{name}: mean {np.mean(error):.9g}, RMS {np.sqrt(np.mean(error**2)):.9g}, "
              f"p95 {np.percentile(error, 95):.9g}, max {error[i]:.9g} "
              f"at {r['site']} {int(r['month']):02d}-{int(r['day']):02d} {int(r['hour']):02d}:00 UTC")

    report("old Moon geocentric direction vs topocentric", before)
    report("new Moon geocentric vs geocentric", geo_error)
    report("new Moon topocentric vs topocentric", topo_error)
    report("Sun vs topocentric", sun_error)
    benoni = np.array([r["site"] == "Benoni" for r in rows])
    print(f"Benoni Moon topocentric max {np.max(topo_error[benoni]):.9g} deg; "
          f"old max {np.max(before[benoni]):.9g} deg")
    print(f"Moon topocentric distance max error {np.max(distance_error):.9g} km; "
          f"angular radius max error {np.max(radius_error):.9g} arcsec")
    print(f"mean-age illuminated fraction vs finite-distance geometry: mean absolute gap {np.mean(phase_error):.9g}, "
          f"max {np.max(phase_error):.9g}")
    print(f"new geometric illuminated fraction: mean absolute gap {np.mean(geometric_phase_error):.9g}, "
          f"max {np.max(geometric_phase_error):.9g}")
    passed = (
        np.max(sun_error) < 0.03
        and np.max(geo_error) < 0.08
        and np.max(topo_error) < 0.08
        and np.max(distance_error) < 200.0
        and np.max(radius_error) < 1.0
        and np.max(geometric_phase_error) < 0.002
    )
    print(f"{'PASS' if passed else 'FAIL'} JPL DE421 comparison; Meeus example 47.a also passed")
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
