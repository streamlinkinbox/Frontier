#!/usr/bin/env python3
"""Audit paired path-proposal RNG results and compare them with baseline/selection-RNG arms."""
from pathlib import Path
import math
import re
import sys

MODES = ("fresh1", "fresh2", "fresh4", "temporal1", "temporal4")
FLOORS = ("default", "lower")
ARMS = ("baseline", "decorrelated")
COHORTS = 128
NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")

def parse(line):
    return dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))

def number(row, field):
    text = row[field]
    if not NUMBER.fullmatch(text):
        raise ValueError(f"invalid numeric {field}={text}")
    value = float(text)
    if not math.isfinite(value):
        raise ValueError(f"non-finite {field}={text}")
    return value

def stats(values):
    mean = sum(values) / len(values)
    variance = sum((value - mean) ** 2 for value in values) / max(1, len(values) - 1)
    return mean, math.sqrt(variance / len(values))

def load(path, prefix):
    batches, results, controls, exits = {}, {}, {}, {}
    for line in Path(path).read_text(errors="replace").splitlines():
        if not line.startswith(prefix + "_"):
            continue
        row = parse(line)
        tag = line.split(" ", 1)[0]
        if tag == prefix + "_BATCH":
            key = (row["arm"], row["mode"], row["floor"], int(row["batch"]))
            if key in batches:
                raise ValueError(f"duplicate {prefix} batch {key}")
            batches[key] = row
        elif tag == prefix + "_RESULT":
            key = (row["arm"], row["mode"], row["floor"])
            if key in results:
                raise ValueError(f"duplicate {prefix} result {key}")
            results[key] = row
        elif tag == prefix + "_CONTROL":
            controls[row["arm"]] = row
        elif tag == prefix + "_ARM_EXIT":
            exits[row["arm"]] = int(row["status"])
    return batches, results, controls, exits

def main(current_path, selection_path):
    batches, results, controls, exits = load(current_path, "REPLAY_PATH_STREAM")
    old_batches, old_results, _, _ = load(selection_path, "REPLAY_SELECTION_STREAM")
    expected_batches = {(arm, mode, floor, n) for arm in ARMS for mode in MODES
                        for floor in FLOORS for n in range(COHORTS)}
    expected_results = {(arm, mode, floor) for arm in ARMS for mode in MODES for floor in FLOORS}
    if set(batches) != expected_batches or set(results) != expected_results:
        raise ValueError(f"path-stream coverage mismatch: {len(batches)} batches, {len(results)} results")
    if set(controls) != set(ARMS) or set(exits) != set(ARMS):
        raise ValueError("missing path-stream control or process exit")
    for arm in ARMS:
        control = controls[arm]
        expected = COHORTS * len(MODES) * len(FLOORS) * 1024
        if int(control["path_seed_checks"]) != expected or int(control["selection_seed_checks"]) != expected:
            raise ValueError(f"{arm}: incomplete path/selection readback checks")
        if int(control["continued_frames"]) != COHORTS * 2 * len(FLOORS) * 3:
            raise ValueError(f"{arm}: temporal continuation coverage mismatch")
        if int(control["minimum_accepted"]) != 1024 or int(control["cap"]) != 1:
            raise ValueError(f"{arm}: cap/acceptance mismatch")

    if len(old_batches) != len(expected_batches) or len(old_results) != len(expected_results):
        raise ValueError("prior selection-stream comparison log is incomplete")
    selection_equal = path_different = previous_baseline_hash_equal = 0
    for mode in MODES:
        for floor in FLOORS:
            for n in range(COHORTS):
                b = batches[("baseline", mode, floor, n)]
                d = batches[("decorrelated", mode, floor, n)]
                old_b = old_batches[("baseline", mode, floor, n)]
                old_d = old_batches[("decorrelated", mode, floor, n)]
                selection_equal += b["selection_hash"] == d["selection_hash"]
                path_different += b["path_hash"] != d["path_hash"]
                previous_baseline_hash_equal += b["hash"] == old_b["hash"]
                if b["selection_hash"] != old_b["selection_hash"]:
                    raise ValueError(f"baseline selection-stream control changed: {mode}/{floor}/{n}")
                if d["selection_hash"] != b["selection_hash"]:
                    raise ValueError(f"path arm altered selection stream: {mode}/{floor}/{n}")
    total = len(MODES) * len(FLOORS) * COHORTS
    if selection_equal != total or path_different < total - 2:
        raise ValueError(f"stream isolation failed: selection-equal={selection_equal}/{total}, path-different={path_different}/{total}")
    if previous_baseline_hash_equal != total:
        raise ValueError(f"new baseline failed exact previous-run replay: {previous_baseline_hash_equal}/{total} image hashes")

    failed = {arm: [] for arm in ARMS}
    for arm in ARMS:
        for mode in MODES:
            for floor in FLOORS:
                key = (arm, mode, floor)
                rows = [batches[(arm, mode, floor, n)] for n in range(COHORTS)]
                summary = results[key]
                reference = number(summary, "reference")
                means = [number(row, "mean") for row in rows]
                mses = [number(row, "mse") for row in rows]
                mean, mean_se = stats(means)
                mse, mse_se = stats(mses)
                rmse = math.sqrt(mse)
                maximum = max(number(row, "max_pixel") for row in rows)
                gate = abs(mean - reference) <= 5 * mean_se + .001
                for field, actual in (("mean", mean), ("mean_se", mean_se), ("mse", mse),
                                      ("mse_se", mse_se), ("rmse", rmse), ("max_pixel", maximum)):
                    if not math.isclose(number(summary, field), actual, rel_tol=2e-9, abs_tol=2e-10):
                        raise ValueError(f"{key}: logged {field} differs from cohort recomputation")
                if int(summary["mean_gate"]) != int(gate) or int(summary["cohorts"]) != COHORTS:
                    raise ValueError(f"{key}: gate/count mismatch")
                if not gate:
                    failed[arm].append(f"{mode}/{floor}")
                print(f"RESULT arm={arm:12s} mode={mode:9s} floor={floor:7s} ref={reference:.9f} "
                      f"mean={mean:.9f}±{mean_se:.9f} RMSE={rmse:.6f} gate={'PASS' if gate else 'FAIL'}")

    for mode in MODES:
        for floor in FLOORS:
            mean_diffs = [number(batches[("decorrelated", mode, floor, n)], "mean") -
                          number(batches[("baseline", mode, floor, n)], "mean") for n in range(COHORTS)]
            mse_diffs = [number(batches[("decorrelated", mode, floor, n)], "mse") -
                         number(batches[("baseline", mode, floor, n)], "mse") for n in range(COHORTS)]
            dm, dme = stats(mean_diffs)
            dq, dqe = stats(mse_diffs)
            print(f"PAIRED mode={mode:9s} floor={floor:7s} delta_mean={dm:+.8f}±{dme:.8f} "
                  f"delta_MSE={dq:+.8f}±{dqe:.8f}")

    print(f"CONTROLS path_hashes_different={path_different}/{total} selection_hashes_equal={selection_equal}/{total} "
          f"baseline_images_match_selection_study={previous_baseline_hash_equal}/{total}")
    for arm in ARMS:
        expected_status = int(bool(failed[arm]))
        if exits[arm] != expected_status:
            raise ValueError(f"{arm}: process exit {exits[arm]} mismatches gate failures {failed[arm]}")
        print(f"ARM_GATE arm={arm} failed={','.join(failed[arm]) or 'none'} exit={exits[arm]} verified=1")

    print("CROSS_STUDY mode floor | baseline | path-stream hash | selection-stream hash")
    for mode in MODES:
        for floor in FLOORS:
            values=[]
            for arm in ("baseline", "decorrelated"):
                row=results[(arm,mode,floor)]
                values.append(f"{number(row,'mean'):.6f}/{number(row,'rmse'):.6f}/{row['mean_gate']}")
            previous=old_results[("decorrelated",mode,floor)]
            values.append(f"{number(previous,'mean'):.6f}/{number(previous,'rmse'):.6f}/{previous['mean_gate']}")
            print(f"CROSS_STUDY {mode:9s} {floor:7s} | " + " | ".join(values))
    if any(failed.values()):
        return 1
    print("PASS ReplayTemporalPathStreamSummary: paired audit and all mean gates passed")
    return 0

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} PATH_STREAM_LOG PRIOR_SELECTION_STREAM_LOG")
    try:
        raise SystemExit(main(sys.argv[1], sys.argv[2]))
    except (KeyError, ValueError, IndexError) as error:
        print(f"FAIL ReplayTemporalPathStreamSummary: {error}", file=sys.stderr)
        raise SystemExit(2)
