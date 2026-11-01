#!/usr/bin/env python3
"""Validate docs and data. Exit code 1 on any error.

Checks:
  1. relative markdown links resolve
  2. SYS-NN / AT-NNN / ADR-NNN / RSK-NN references point to defined IDs
  3. data/*.csv schema, enums, uniqueness and cross-references
  4. generated tables are up to date
"""
import csv
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
errors = []


def err(msg):
    errors.append(msg)


def md_files():
    return sorted(p for p in ROOT.rglob("*.md") if ".git" not in p.parts)


def slug(heading):
    text = re.sub(r"[^\w\s-]", "", heading.strip().lower())
    return re.sub(r"\s", "-", text)


def anchors(path):
    found = set()
    in_code = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.lstrip().startswith("```"):
            in_code = not in_code
        m = re.match(r"^#{1,6}\s+(.*)$", line)
        if m and not in_code:
            found.add(slug(m.group(1)))
    return found


def check_links():
    link = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
    for p in md_files():
        in_code = False
        for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            if line.lstrip().startswith("```"):
                in_code = not in_code
            if in_code:
                continue
            for target in link.findall(line):
                if re.match(r"^(https?:|mailto:)", target):
                    continue
                path, _, frag = target.partition("#")
                dest = (p.parent / path) if path else p
                if not dest.exists():
                    err(f"{p.relative_to(ROOT)}:{n}: broken link -> {target}")
                elif frag and dest.suffix == ".md" and frag not in anchors(dest):
                    err(f"{p.relative_to(ROOT)}:{n}: missing anchor -> {target}")


DEFS = {
    "SYS": ("docs/04_SYSTEMS_SPEC.md", re.compile(r"^## (SYS-\d{2})\b", re.M)),
    "AT": ("docs/08_QA_AND_ACCEPTANCE.md", re.compile(r"^\| (AT-\d{3}) \|", re.M)),
    "ADR": ("docs/10_DECISIONS_AND_LEGACY_MAP.md", re.compile(r"^### (ADR-\d{3})\b", re.M)),
    "RSK": ("docs/09_RISKS.md", re.compile(r"^\| (RSK-\d{2}) \|", re.M)),
}
REFS = {
    "SYS": re.compile(r"(?<![A-Z-])SYS-\d{2}\b"),
    "AT": re.compile(r"(?<![A-Z-])AT-\d{3}\b"),
    "ADR": re.compile(r"(?<![A-Z-])ADR-\d{3}\b"),
    "RSK": re.compile(r"(?<![A-Z-])RSK-\d{2}\b"),
}


def check_ids():
    defined = {}
    for kind, (rel, rx) in DEFS.items():
        path = ROOT / rel
        if not path.exists():
            err(f"missing file for {kind} definitions: {rel}")
            defined[kind] = set()
            continue
        ids = rx.findall(path.read_text(encoding="utf-8"))
        if len(ids) != len(set(ids)):
            err(f"{rel}: duplicate {kind} ids")
        defined[kind] = set(ids)
    for p in md_files():
        text = p.read_text(encoding="utf-8")
        for kind, rx in REFS.items():
            for ref in set(rx.findall(text)):
                if ref not in defined[kind]:
                    err(f"{p.relative_to(ROOT)}: reference to undefined {ref}")


def load(name):
    with open(ROOT / "data" / name, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def check_data():
    releases = {"slice", "alpha", "ea"}
    rarities = {"common", "uncommon", "rare", "very_rare"}
    tiers = {"R0", "R1", "R2", "R3", "R4"}

    poi = load("poi.csv")
    if len(poi) != 27:
        err(f"poi.csv: expected 27 rows, found {len(poi)}")
    sett = load("settlements.csv")
    if len(sett) != 14:
        err(f"settlements.csv: expected 14 rows, found {len(sett)}")
    for name, rows, key in (("poi.csv", poi, "poi_id"), ("settlements.csv", sett, "settlement_id")):
        ids = [r[key] for r in rows]
        if len(ids) != len(set(ids)):
            err(f"{name}: duplicate ids")
        for r in rows:
            if r["risk_tier"] not in tiers:
                err(f"{name}: {r[key]} bad risk_tier {r['risk_tier']}")
            if r["release"] not in releases:
                err(f"{name}: {r[key]} bad release {r['release']}")
            if not (0 <= float(r["x_km"]) <= 10 and 0 <= float(r["y_km"]) <= 9):
                err(f"{name}: {r[key]} outside the 10x9 km technical area")

    ammo = load("ammo.csv")
    weapons = load("weapons.csv")
    armor = load("armor.csv")
    for name, rows, key in (("ammo.csv", ammo, "ammo_id"), ("weapons.csv", weapons, "weapon_id"), ("armor.csv", armor, "armor_id")):
        ids = [r[key] for r in rows]
        if len(ids) != len(set(ids)):
            err(f"{name}: duplicate ids")
        for r in rows:
            if r["rarity"] not in rarities:
                err(f"{name}: {r[key]} bad rarity {r['rarity']}")
            if r["release"] not in releases:
                err(f"{name}: {r[key]} bad release {r['release']}")
    calibers = {a["caliber"] for a in ammo}
    for w in weapons:
        if w["class"] not in {"melee", "throwable"} and w["caliber"] not in calibers:
            err(f"weapons.csv: {w['weapon_id']} caliber {w['caliber']} has no ammo in ammo.csv")
    used = {w["caliber"] for w in weapons}
    for c in calibers - used:
        err(f"ammo.csv: caliber {c} is not used by any weapon")
    for a in armor:
        if not 1 <= int(a["armor_class"]) <= 6:
            err(f"armor.csv: {a['armor_id']} class out of range")
    for a in ammo:
        if not 1 <= int(a["pen_tier"]) <= 6:
            err(f"ammo.csv: {a['ammo_id']} pen_tier out of range")


def check_generated():
    res = subprocess.run([sys.executable, str(ROOT / "tools" / "gen_tables.py"), "--check"], capture_output=True, text=True)
    if res.returncode != 0:
        err((res.stdout + res.stderr).strip())


def main():
    check_links()
    check_ids()
    check_data()
    check_generated()
    if errors:
        print(f"{len(errors)} error(s):")
        for e in errors:
            print(" -", e)
        return 1
    print(f"OK: {len(md_files())} markdown files, data and generated tables consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
