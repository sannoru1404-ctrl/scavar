#pragma once
// Item table (data/items.csv): inventory size, stack, price, Central Economy parameters. Cross-checked against weapons/ammo/armor tables.
#include "GameData.h"
#include "Inventory.h"
#include "LootEconomy.h"
#include <string>
#include <vector>

namespace Scavar
{
struct ItemRow
{
    std::string Id, Name, Category;
    int W = 1, H = 1;
    float WeightKg = 0;
    int StackMax = 1;
    int GridW = 0, GridH = 0;     // storage grid of packs/vests; 0 for non-containers
    int BasePrice = 0;
    int Nominal = 0, Min = 0;
    double LifetimeS = 3600;
    std::vector<std::string> Usage;
    uint8_t Tiers = AllTiersMask;
    Rarity Rarity_ = Rarity::Common;
    Release Release_ = Release::Slice;
    int QtyMin = 1, QtyMax = 1;   // quantity of a freshly spawned stack
};

struct ItemTable
{
    std::vector<ItemRow> Rows;

    const ItemRow* Find(const std::string& id) const;
    bool Load(const std::string& dataDir, const GameData& game, std::string* error);
    bool BuildCatalog(ItemCatalog& out) const;
    // Loot types for every row with Release <= maxRelease and Nominal > 0.
    std::vector<LootTypeDef> BuildLootTypes(const GameData& game, Release maxRelease) const;
};
} // namespace Scavar
