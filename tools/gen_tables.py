#!/usr/bin/env python3
"""Regenerate markdown tables embedded in docs from data/*.csv.

Usage:
  python tools/gen_tables.py          # rewrite docs in place
  python tools/gen_tables.py --check  # exit 1 if any block is stale
"""
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(name):
    with open(ROOT / "data" / name, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def md_table(headers, rows):
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    out += ["| " + " | ".join(str(c).replace("|", "/") for c in r) + " |" for r in rows]
    return "\n".join(out)


def poi_table():
    rows = [(r["poi_id"], r["name_ru"], f'{r["x_km"]} / {r["y_km"]}', r["risk_tier"], r["release"], r["role_ru"])
            for r in read("poi.csv")]
    return md_table(["ID", "Название", "X / Y, км", "Риск", "Релиз", "Роль в игре"], rows)


def settlements_table():
    rows = [(r["settlement_id"], r["name_ru"], r["kind"], f'{r["x_km"]} / {r["y_km"]}', r["risk_tier"], r["release"])
            for r in read("settlements.csv")]
    return md_table(["ID", "Название", "Тип", "X / Y, км", "Риск", "Релиз"], rows)


def weapons_table():
    rows = [(r["weapon_id"], r["name_ru"], r["class"], r["caliber"], r["mag_size"], r["rpm"],
             r["effective_range_m"], r["rarity"], r["release"]) for r in read("weapons.csv")]
    return md_table(["ID", "Оружие", "Класс", "Калибр", "Магазин", "RPM", "Дистанция, м", "Редкость", "Релиз"], rows)


def ammo_table():
    rows = [(r["ammo_id"], r["name_ru"], r["damage"], r["pen_tier"], r["muzzle_velocity_mps"], r["pellets"],
             r["rarity"], r["release"]) for r in read("ammo.csv")]
    return md_table(["ID", "Патрон", "Урон", "Пробитие", "V0, м/с", "Дробин", "Редкость", "Релиз"], rows)


def armor_table():
    rows = [(r["armor_id"], r["name_ru"], r["slot"], r["armor_class"], r["durability"], r["weight_kg"],
             r["rarity"], r["release"]) for r in read("armor.csv")]
    return md_table(["ID", "Предмет", "Слот", "Класс", "Прочность", "Вес, кг", "Редкость", "Релиз"], rows)


BLOCKS = {
    "POI_TABLE": ("docs/02_LORE_CANON.md", poi_table),
    "SETTLEMENTS_TABLE": ("docs/02_LORE_CANON.md", settlements_table),
    "WEAPONS_TABLE": ("docs/05_CONTENT_PLAN.md", weapons_table),
    "AMMO_TABLE": ("docs/05_CONTENT_PLAN.md", ammo_table),
    "ARMOR_TABLE": ("docs/05_CONTENT_PLAN.md", armor_table),
}


def render(path, text, names):
    for name in names:
        pattern = re.compile(rf"(<!-- BEGIN:{name} -->\n).*?(\n<!-- END:{name} -->)", re.S)
        if not pattern.search(text):
            raise SystemExit(f"{path}: marker block {name} not found")
        text = pattern.sub(lambda m: m.group(1) + BLOCKS[name][1]() + m.group(2), text)
    return text


def main():
    check = "--check" in sys.argv
    files = {}
    for name, (rel, _) in BLOCKS.items():
        files.setdefault(rel, []).append(name)
    stale = []
    for rel, names in files.items():
        path = ROOT / rel
        old = path.read_text(encoding="utf-8")
        new = render(rel, old, names)
        if new != old:
            stale.append(rel)
            if not check:
                path.write_text(new, encoding="utf-8")
    if check and stale:
        print("STALE generated tables (run tools/gen_tables.py): " + ", ".join(stale))
        return 1
    print("generated tables up to date" if check else f"updated: {', '.join(stale) or 'nothing to change'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
