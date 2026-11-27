#!/usr/bin/env python3
"""Check a captured Mission 0 stair descent from the upper landing."""
import argparse
import csv
import math
import sys


def capsule_clearance_lower_bound(sample):
    """Minimum possible capsule-to-plane clearance over all yaw angles."""
    ny = float(sample["surface_ny"])
    horizontal_normal = math.hypot(float(sample["surface_nx"]), float(sample["surface_nz"]))
    origin_height = float(sample["y"]) - float(sample["ray_y"])
    endpoints = (
        (float(sample[axis + "y"]),
         math.hypot(float(sample[axis + "x"]), float(sample[axis + "z"])),
         float(sample[axis + "w"]))
        for axis in ("a", "b")
    )
    return min(
        ny * (origin_height + height) - horizontal_normal * offset - radius
        for height, offset, radius in endpoints
    )


def check_crouch_gravity(descent):
    deltas = []
    for previous, current in zip(descent, descent[1:]):
        if not (
            previous["grounded"] == current["grounded"] == "0"
            and previous["next_clip"].startswith("crouch_")
            and current["next_clip"].startswith("crouch_")
            and previous["solver_vy_out"] and current["guest_dt"]
        ):
            continue
        incoming = float(previous["solver_vy"])
        outgoing = float(previous["solver_vy_out"])
        dt = float(current["guest_dt"])
        elapsed = float(current["time"]) - float(previous["time"])
        if (incoming >= 0 or abs(incoming - outgoing) > 0.0001
                or dt <= 0 or abs(elapsed - dt) > 0.002):
            continue
        delta = float(current["requested_vy"]) - outgoing
        deltas.append(abs(delta + 19.6 * dt))
    if not deltas:
        print("No consecutive unsupported crouch updates without solver contact.")
        return 2
    mismatches = sum(error > 0.02 for error in deltas)
    print(f"{mismatches}/{len(deltas)} unsupported crouch steps lost vertical momentum")
    return int(bool(mismatches))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="CSV emitted by the stair contact probe")
    parser.add_argument("start", type=float, help="first timestamp in the descent")
    parser.add_argument("stop", type=float, help="last timestamp in the descent")
    parser.add_argument("--crouch", action="store_true")
    parser.add_argument("--crouch-gravity", action="store_true",
                        help="check airborne momentum separately from continuous support")
    args = parser.parse_args()
    with open(args.trace, newline="") as trace:
        samples = [
            row for row in csv.DictReader(trace)
            if args.start <= float(row["time"]) <= args.stop
        ]
    descent = [row for row in samples if 88.0 <= float(row["z"]) <= 93.5]
    if (not samples or not descent or float(samples[0]["z"]) >= 88.0
            or float(samples[-1]["z"]) < 93.5):
        print("Capture both the approach from the upper landing and the full ramp.", file=sys.stderr)
        return 2

    if args.crouch_gravity:
        return check_crouch_gravity(descent)

    failures = []
    airborne = sum(row["grounded"] == "0" for row in descent)
    if airborne:
        failures.append(f"{airborne}/{len(descent)} ramp updates lost support")
    if any(row["next_clip"] == "falling" for row in descent):
        failures.append("falling appeared during the descent")

    if args.crouch:
        crouched = [row for row in descent if row["next_clip"].startswith("crouch_")]
        if len(crouched) < len(descent) * 0.8:
            failures.append("crouch locomotion did not cover most of the descent")
        contacts = [
            row for row in crouched
            if row["ray_hit"] == "1" and float(row["surface_ny"]) >= float(row["slope_cos"])
        ]
        if not contacts or not all(float(row["aw"]) > 0 for row in contacts):
            print("A crouch trace requires a valid capsule and surface ray.", file=sys.stderr)
            return 2
        max_clearance = max(map(capsule_clearance_lower_bound, contacts))
        # The guest support query expands its collision tolerance by 0.15 units.
        if max_clearance > 0.15:
            failures.append(f"capsule clearance reached at least {max_clearance:.3f} units")
    for failure in failures:
        print(failure)
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
