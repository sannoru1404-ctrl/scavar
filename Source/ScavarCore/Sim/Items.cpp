#include "Items.h"
#include "Csv.h"
#include <charconv>
#include <cmath>
#include <set>

namespace Scavar
{
namespace
{
bool ToInt(const std::string& s, int& out) { auto r = std::from_chars(s.data(), s.data() + s.size(), out); return r.ec == std::errc() && r.ptr == s.data() + s.size(); }
bool ToDouble(const std::string& s, double& out) { auto r = std::from_chars(s.data(), s.data() + s.size(), out); return r.ec == std::errc() && r.ptr == s.data() + s.size(); }

std::vector<std::string> SplitBar(const std::string& s)
{
    std::vector<std::string> parts;
    if (s.empty()) return parts;
    size_t start = 0;
    while (true)
    {
        const size_t p = s.find('|', start);
        parts.push_back(s.substr(start, p == std::string::npos ? p : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return parts;
}

bool ParseRarity(const std::string& s, Rarity& out)
{
    if (s == "common") out = Rarity::Common; else if (s == "uncommon") out = Rarity::Uncommon;
    else if (s == "rare") out = Rarity::Rare; else if (s == "very_rare") out = Rarity::VeryRare; else return false;
    return true;
}
bool ParseRelease(const std::string& s, Release& out)
{
    if (s == "slice") out = Release::Slice; else if (s == "alpha") out = Release::Alpha; else if (s == "ea") out = Release::EA; else return false;
    return true;
}
bool IsKnownCategory(const std::string& c)
{
    static const std::set<std::string> known{"weapon", "ammo", "magazine", "armor", "medical", "food", "clothing", "pack", "tool", "misc"};
    return known.count(c) > 0;
}
#define ITEMS_FAIL(msg) do { if (error) *error = (msg); return false; } while (0)
} // namespace

const ItemRow* ItemTable::Find(const std::string& id) const
{
    for (const auto& r : Rows) if (r.Id == id) return &r;
    return nullptr;
}

bool ItemTable::Load(const std::string& dir, const GameData& game, std::string* error)
{
    Rows.clear();
    CsvTable t;
    std::string err;
    if (!LoadCsvFile(dir + "/items.csv", t, &err)) ITEMS_FAIL(err);

    for (size_t r = 0; r < t.Rows.size(); ++r)
    {
        ItemRow row;
        row.Id = t.Cell(r, "item_id"); row.Name = t.Cell(r, "name_ru"); row.Category = t.Cell(r, "category");
        double weight = 0, lifetime = 0;
        if (!ToInt(t.Cell(r, "w"), row.W) || !ToInt(t.Cell(r, "h"), row.H) || !ToDouble(t.Cell(r, "weight_kg"), weight) ||
            !ToInt(t.Cell(r, "stack_max"), row.StackMax) || !ToInt(t.Cell(r, "grid_w"), row.GridW) || !ToInt(t.Cell(r, "grid_h"), row.GridH) ||
            !ToInt(t.Cell(r, "base_price"), row.BasePrice) || !ToInt(t.Cell(r, "nominal"), row.Nominal) || !ToInt(t.Cell(r, "min"), row.Min) ||
            !ToDouble(t.Cell(r, "lifetime_s"), lifetime) || !ToInt(t.Cell(r, "qty_min"), row.QtyMin) || !ToInt(t.Cell(r, "qty_max"), row.QtyMax) ||
            !ParseRarity(t.Cell(r, "rarity"), row.Rarity_) || !ParseRelease(t.Cell(r, "release"), row.Release_))
            ITEMS_FAIL("items.csv: bad value in row for " + row.Id);
        row.WeightKg = static_cast<float>(weight);
        row.LifetimeS = lifetime;
        row.Usage = SplitBar(t.Cell(r, "usage"));

        row.Tiers = 0;
        for (const auto& tier : SplitBar(t.Cell(r, "tiers")))
        {
            if (tier.size() != 2 || tier[0] != 'R' || tier[1] < '0' || tier[1] > '4') ITEMS_FAIL("items.csv: bad tier '" + tier + "' for " + row.Id);
            row.Tiers = static_cast<uint8_t>(row.Tiers | TierBit(static_cast<RiskTier>(tier[1] - '0')));
        }

        if (row.Id.empty() || Find(row.Id)) ITEMS_FAIL("items.csv: empty or duplicate id " + row.Id);
        if (!IsKnownCategory(row.Category)) ITEMS_FAIL("items.csv: unknown category for " + row.Id);
        if (row.W < 1 || row.H < 1 || row.StackMax < 1 || row.BasePrice < 0) ITEMS_FAIL("items.csv: bad size/stack/price for " + row.Id);
        if (row.Min > row.Nominal) ITEMS_FAIL("items.csv: min > nominal for " + row.Id);
        if (row.QtyMin < 1 || row.QtyMax < row.QtyMin || row.QtyMax > row.StackMax) ITEMS_FAIL("items.csv: bad spawn quantity for " + row.Id);
        if (row.Tiers == 0) ITEMS_FAIL("items.csv: no tiers for " + row.Id);
        if (row.Nominal > 0 && row.Usage.empty()) ITEMS_FAIL("items.csv: lootable item without usage tags: " + row.Id);
        const bool isContainer = row.Category == "pack";
        if (isContainer != (row.GridW > 0 && row.GridH > 0)) ITEMS_FAIL("items.csv: grid size must be set exactly for packs: " + row.Id);
        Rows.push_back(std::move(row));
    }

    // Cross-checks against the typed tables so that stats live in exactly one place.
    for (const auto& row : Rows)
    {
        if (row.Category == "weapon" && !game.FindWeapon(row.Id)) ITEMS_FAIL("items.csv: weapon not in weapons.csv: " + row.Id);
        if (row.Category == "ammo" && !game.FindAmmo(row.Id)) ITEMS_FAIL("items.csv: ammo not in ammo.csv: " + row.Id);
        if (row.Category == "armor")
        {
            const ArmorDef* a = game.FindArmor(row.Id);
            if (!a) ITEMS_FAIL("items.csv: armor not in armor.csv: " + row.Id);
            if (std::fabs(a->WeightKg - row.WeightKg) > 1e-4f) ITEMS_FAIL("items.csv: armor weight differs from armor.csv: " + row.Id);
        }
        if (const WeaponDef* w = game.FindWeapon(row.Id); w && w->Rarity_ != row.Rarity_) ITEMS_FAIL("items.csv: rarity differs from weapons.csv: " + row.Id);
        if (const AmmoDef* a = game.FindAmmo(row.Id); a && a->Rarity_ != row.Rarity_) ITEMS_FAIL("items.csv: rarity differs from ammo.csv: " + row.Id);
        if (const ArmorDef* a = game.FindArmor(row.Id); a && a->Rarity_ != row.Rarity_) ITEMS_FAIL("items.csv: rarity differs from armor.csv: " + row.Id);
    }
    for (const auto& w : game.Weapons)
        if (w.Release_ == Release::Slice && !Find(w.Id)) ITEMS_FAIL("weapons.csv slice weapon missing from items.csv: " + w.Id);
    for (const auto& a : game.Ammo)
        if (a.Release_ == Release::Slice && !Find(a.Id)) ITEMS_FAIL("ammo.csv slice ammo missing from items.csv: " + a.Id);
    for (const auto& a : game.Armor)
        if (a.Release_ == Release::Slice && !Find(a.Id)) ITEMS_FAIL("armor.csv slice armor missing from items.csv: " + a.Id);
    return true;
}

bool ItemTable::BuildCatalog(ItemCatalog& out) const
{
    for (const auto& r : Rows)
    {
        ItemDef d;
        d.Id = r.Id; d.W = r.W; d.H = r.H; d.Weight = r.WeightKg; d.StackMax = r.StackMax;
        if (!out.Register(d)) return false;
    }
    return true;
}

std::vector<LootTypeDef> ItemTable::BuildLootTypes(const GameData& game, Release maxRelease) const
{
    std::vector<LootTypeDef> types;
    for (const auto& r : Rows)
    {
        if (r.Nominal <= 0 || static_cast<int>(r.Release_) > static_cast<int>(maxRelease)) continue;
        LootTypeDef t;
        t.DefId = r.Id; t.Nominal = r.Nominal; t.Min = r.Min; t.LifetimeS = r.LifetimeS;
        t.UsageTags = r.Usage; t.AllowedTiers = r.Tiers; t.Rarity_ = r.Rarity_;
        const int area = r.W * r.H;
        t.Size = area <= 4 ? LootSize::Small : (area <= 9 ? LootSize::Medium : LootSize::Large);
        const WeaponDef* w = game.FindWeapon(r.Id);
        t.IsWeapon = w && w->Class != "melee" && w->Class != "throwable";
        types.push_back(std::move(t));
    }
    return types;
}
#undef ITEMS_FAIL
} // namespace Scavar
