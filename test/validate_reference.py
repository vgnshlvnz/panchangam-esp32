#!/usr/bin/env python3
"""Validate test/reference.json against test/SCHEMA.md (structure + pass-criteria 4)."""
import json
import sys
from pathlib import Path

RANGES = {"tithi": 30, "nakshatra": 27, "yoga": 27, "karana": 60}
CAP = {"tithi": 4, "nakshatra": 4, "yoga": 4, "karana": 8}
PART = {"rahu_kalam": (8, 2, 7, 5, 6, 4, 3), "yamagandam": (5, 4, 3, 2, 1, 7, 6),
        "gulika": (7, 6, 5, 4, 3, 2, 1)}
errs = []


def err(case, msg):
    errs.append(f"{case}: {msg}")


def inst(c, o, what):
    if not (isinstance(o, dict) and isinstance(o.get("jd_ut"), float) and isinstance(o.get("iso"), str)):
        err(c, f"{what}: bad Instant {o!r}")
        return None
    return o["jd_ut"]


def main():
    doc = json.loads((Path(__file__).parent / "reference.json").read_text())
    m = doc["meta"]
    for k in ("generator", "swisseph_version", "ayanamsa", "ephemeris",
              "sunrise_definition", "place", "tolerance"):
        if k not in m:
            errs.append(f"meta.{k} missing")
    for k in ("name", "latitude_deg", "longitude_deg", "elevation_m", "timezone"):
        if k not in m["place"]:
            errs.append(f"meta.place.{k} missing")
    dates = [c["date"] for c in doc["cases"]]
    if dates != sorted(set(dates)):
        errs.append("cases not strictly ascending by date")
    eps = 1e-6  # days (~0.09 s)
    for case in doc["cases"]:
        c = case["date"]
        i = case["input"]
        if not all(isinstance(i.get(k), int) for k in ("year", "month", "day")):
            err(c, "input date ints")
        if f"{i['year']:04d}-{i['month']:02d}-{i['day']:02d}" != c:
            err(c, "input date != date")
        ds, ns = i["day_start_jd_ut"], i["next_day_start_jd_ut"]
        if not 0.5 < ns - ds < 1.5:
            err(c, "next_day_start - day_start not ~1 day")
        sr, ss, nsr = (inst(c, case[k], k) for k in ("sunrise", "sunset", "next_sunrise"))
        if None in (sr, ss, nsr) or not sr < ss < nsr:
            err(c, "need sunrise < sunset < next_sunrise")
            continue
        v = case["vara"]
        if not 1 <= v["index"] <= 7:
            err(c, "vara index")
        if abs(inst(c, v["start"], "vara.start") - sr) > eps or abs(inst(c, v["end"], "vara.end") - nsr) > eps:
            err(c, "vara != sunrise..next_sunrise")
        for key, top in RANGES.items():
            sp = case[key]
            if not 1 <= len(sp) <= CAP[key]:
                err(c, f"{key} count {len(sp)}")
            for j, s in enumerate(sp):
                if not (isinstance(s["index"], int) and 1 <= s["index"] <= top and isinstance(s["name"], str)):
                    err(c, f"{key}[{j}] index/name")
                a, b = inst(c, s["start"], key), inst(c, s["end"], key)
                if a is None or b is None or not a < b:
                    err(c, f"{key}[{j}] start<end")
                if j and abs(a - sp[j - 1]["end"]["jd_ut"]) > eps:
                    err(c, f"{key}[{j}] not contiguous")
            if sp[0]["start"]["jd_ut"] > sr + eps:
                err(c, f"{key} first span starts after sunrise")
            if sp[-1]["end"]["jd_ut"] < nsr - eps:
                err(c, f"{key} last span ends before next sunrise")
        wk = (v["index"] - 1)  # Sunday-first, same as tables
        for key, table in PART.items():
            p = case[key]
            a, b = inst(c, p["start"], key), inst(c, p["end"], key)
            eighth = (ss - sr) / 8
            n = table[wk]
            if abs(a - (sr + (n - 1) * eighth)) > eps or abs(b - a - eighth) > eps:
                err(c, f"{key} is not eighth #{n} of daylight")
    print(f"{len(doc['cases'])} cases, {len(errs)} error(s)")
    for e in errs[:20]:
        print(" ", e)
    sys.exit(1 if errs else 0)


main()
