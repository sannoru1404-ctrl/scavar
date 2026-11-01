#include "GameData.h"
#include "Csv.h"
#include <charconv>
#include <set>

namespace Scavar
{
namespace
{
bool ToInt(const std::string& s, int& out) { auto r = std::from_chars(s.data(), s.data() + s.size(), out); return r.ec == std::errc() && r.ptr == s.data() + s.size(); }
bool ToDouble(const std::string& s, double& out) { auto r = std::from_chars(s.data(), s.data() + s.size(), out); return r.ec == std::errc() && r.ptr == s.data() + s.size(); }
bool ToFloat(const std::string& s, float& out) { double d; if (!ToDouble(s, d)) return false; out = static_cast<float>(d); return true; }

bool ToRarity(const std::string& s, Rarity& out)
{
    if (s == "common") out = Rarity::Common; else if (s == "uncommon") out = Rarity::Uncommon;
    else if (s == "rare") out = Rarity::Rare; else if (s == "very_rare") out = Rarity::VeryRare; else return false;
    return true;
}
bool ToRelease(const std::string& s, Release& out)
{
    if (s == "slice") out = Release::Slice; else if (s == "alpha") out = Release::Alpha; else if (s == "ea") out = Release::EA; else return false;
    return true;
}
bool ToTier(const std::string& s, RiskTier& out)
{
    if (s.size() != 2 || s[0] != 'R' || s[1] < '0' || s[1] > '4') return false;
    out = static_cast<RiskTier>(s[1] - '0');
    return true;
}
std::vector<std::string> Split(const std::string& s, char sep)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) { const size_t p = s.find(sep, start); parts.push_back(s.substr(start, p == std::string::npos ? p : p - start)); if (p == std::string::npos) break; start = p + 1; }
    return parts;
}
#define FAIL(msg) do { if (error) *error = (msg); return false; } while (0)
} // namespace

const AmmoDef* GameData::FindAmmo(const std::string& id) const { for (const auto& a : Ammo) if (a.Id == id) return &a; return nullptr; }
const WeaponDef* GameData::FindWeapon(const std::string& id) const { for (const auto& w : Weapons) if (w.Id == id) return &w; return nullptr; }
const ArmorDef* GameData::FindArmor(const std::string& id) const { for (const auto& a : Armor) if (a.Id == id) return &a; return nullptr; }
const PoiDef* GameData::FindPoi(const std::string& id) const { for (const auto& p : Pois) if (p.Id == id) return &p; return nullptr; }

bool GameData::Load(const std::string& dir, std::string* error)
{
    *this = GameData{};
    std::string err;
    CsvTable t;

    if (!LoadCsvFile(dir + "/ammo.csv", t, &err)) FAIL(err);
    for (size_t r = 0; r < t.Rows.size(); ++r)
    {
        AmmoDef a;
        a.Id = t.Cell(r, "ammo_id"); a.Caliber = t.Cell(r, "caliber"); a.Name = t.Cell(r, "name_ru");
        if (!ToFloat(t.Cell(r, "damage"), a.Damage) || !ToInt(t.Cell(r, "pen_tier"), a.PenTier) ||
            !ToFloat(t.Cell(r, "muzzle_velocity_mps"), a.MuzzleVelocity) || !ToInt(t.Cell(r, "pellets"), a.Pellets) ||
            !ToRarity(t.Cell(r, "rarity"), a.Rarity_) || !ToRelease(t.Cell(r, "release"), a.Release_))
            FAIL("ammo.csv: bad value in row for " + a.Id);
        if (a.PenTier < 1 || a.PenTier > 6) FAIL("ammo.csv: pen_tier out of range for " + a.Id);
        if (FindAmmo(a.Id)) FAIL("ammo.csv: duplicate id " + a.Id);
        Ammo.push_back(std::move(a));
    }

    if (!LoadCsvFile(dir + "/weapons.csv", t, &err)) FAIL(err);
    for (size_t r = 0; r < t.Rows.size(); ++r)
    {
        WeaponDef w;
        w.Id = t.Cell(r, "weapon_id"); w.Name = t.Cell(r, "name_ru"); w.Class = t.Cell(r, "class"); w.Caliber = t.Cell(r, "caliber");
        w.FireModes = Split(t.Cell(r, "fire_modes"), '|');
        if (!ToInt(t.Cell(r, "mag_size"), w.MagSize) || !ToFloat(t.Cell(r, "rpm"), w.Rpm) ||
            !ToFloat(t.Cell(r, "effective_range_m"), w.EffectiveRangeM) ||
            !ToRarity(t.Cell(r, "rarity"), w.Rarity_) || !ToRelease(t.Cell(r, "release"), w.Release_))
            FAIL("weapons.csv: bad value in row for " + w.Id);
        if (FindWeapon(w.Id)) FAIL("weapons.csv: duplicate id " + w.Id);
        const bool needsAmmo = w.Class != "melee" && w.Class != "throwable";
        if (needsAmmo)
        {
            bool found = false;
            for (const auto& a : Ammo) found = found || a.Caliber == w.Caliber;
            if (!found) FAIL("weapons.csv: " + w.Id + " caliber " + w.Caliber + " has no ammo");
        }
        Weapons.push_back(std::move(w));
    }

    if (!LoadCsvFile(dir + "/armor.csv", t, &err)) FAIL(err);
    for (size_t r = 0; r < t.Rows.size(); ++r)
    {
        ArmorDef a;
        a.Id = t.Cell(r, "armor_id"); a.Name = t.Cell(r, "name_ru"); a.Slot = t.Cell(r, "slot");
        if (!ToInt(t.Cell(r, "armor_class"), a.ArmorClass) || !ToFloat(t.Cell(r, "durability"), a.Durability) ||
            !ToFloat(t.Cell(r, "weight_kg"), a.WeightKg) ||
            !ToRarity(t.Cell(r, "rarity"), a.Rarity_) || !ToRelease(t.Cell(r, "release"), a.Release_))
            FAIL("armor.csv: bad value in row for " + a.Id);
        if (a.ArmorClass < 1 || a.ArmorClass > 6) FAIL("armor.csv: class out of range for " + a.Id);
        if (a.Slot != "helmet" && a.Slot != "vest") FAIL("armor.csv: unknown slot for " + a.Id);
        if (FindArmor(a.Id)) FAIL("armor.csv: duplicate id " + a.Id);
        Armor.push_back(std::move(a));
    }

    if (!LoadCsvFile(dir + "/poi.csv", t, &err)) FAIL(err);
    for (size_t r = 0; r < t.Rows.size(); ++r)
    {
        PoiDef p;
        p.Id = t.Cell(r, "poi_id"); p.Name = t.Cell(r, "name_ru"); p.Role = t.Cell(r, "role_ru");
        if (!ToDouble(t.Cell(r, "x_km"), p.XKm) || !ToDouble(t.Cell(r, "y_km"), p.YKm) ||
            !ToTier(t.Cell(r, "risk_tier"), p.Tier) || !ToRelease(t.Cell(r, "release"), p.Release_))
            FAIL("poi.csv: bad value in row for " + p.Id);
        if (p.XKm < 0 || p.XKm > 10 || p.YKm < 0 || p.YKm > 9) FAIL("poi.csv: " + p.Id + " outside 10x9 km area");
        if (FindPoi(p.Id)) FAIL("poi.csv: duplicate id " + p.Id);
        Pois.push_back(std::move(p));
    }
    return true;
}
} // namespace Scavar

#undef FAIL
