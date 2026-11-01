#pragma once
#include "Types.h"
#include <string>
#include <vector>

namespace Scavar
{
struct AmmoDef
{
    std::string Id, Caliber, Name;
    float Damage = 0;
    int PenTier = 1;
    float MuzzleVelocity = 0;
    int Pellets = 1;
    Rarity Rarity_ = Rarity::Common;
    Release Release_ = Release::Slice;
};

struct WeaponDef
{
    std::string Id, Name, Class, Caliber;
    int MagSize = 0;
    std::vector<std::string> FireModes;
    float Rpm = 0, EffectiveRangeM = 0;
    Rarity Rarity_ = Rarity::Common;
    Release Release_ = Release::Slice;
};

struct ArmorDef
{
    std::string Id, Name, Slot;
    int ArmorClass = 1;
    float Durability = 0, WeightKg = 0;
    Rarity Rarity_ = Rarity::Common;
    Release Release_ = Release::Slice;
};

struct PoiDef
{
    std::string Id, Name, Role;
    double XKm = 0, YKm = 0;
    RiskTier Tier = RiskTier::R1;
    Release Release_ = Release::Slice;
};

// Immutable game balance tables loaded from data/*.csv (single source of truth, ADR-021).
struct GameData
{
    std::vector<AmmoDef> Ammo;
    std::vector<WeaponDef> Weapons;
    std::vector<ArmorDef> Armor;
    std::vector<PoiDef> Pois;

    const AmmoDef* FindAmmo(const std::string& id) const;
    const WeaponDef* FindWeapon(const std::string& id) const;
    const ArmorDef* FindArmor(const std::string& id) const;
    const PoiDef* FindPoi(const std::string& id) const;

    // Loads and cross-validates ammo/weapons/armor/poi from `dataDir`. Returns false and fills `error` on failure.
    bool Load(const std::string& dataDir, std::string* error);
};
} // namespace Scavar
