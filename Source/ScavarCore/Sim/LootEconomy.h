#pragma once
// SYS-04 Central Economy: periodic restock of world loot, DayZ-style, with hard caps.
// Pure logic: the caller owns the world, applies the returned decisions and feeds the snapshot back.
#include "Rng.h"
#include "Types.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Scavar
{
enum class LootSize : uint8_t { Small = 0, Medium, Large };

constexpr uint8_t TierBit(RiskTier t) { return static_cast<uint8_t>(1u << static_cast<unsigned>(t)); }
constexpr uint8_t AllTiersMask = 0x1F;

struct LootTypeDef
{
    std::string DefId;
    int Nominal = 0;                    // hard cap for the whole world
    int Min = 0;                        // restock starts when count < Min
    double LifetimeS = 3600.0;          // ground lifetime
    std::vector<std::string> UsageTags; // empty = fits any point
    uint8_t AllowedTiers = AllTiersMask;
    Rarity Rarity_ = Rarity::Common;
    LootSize Size = LootSize::Small;    // needs a point with Size >= this
    bool IsWeapon = false;
};

struct LootSpawnPoint
{
    int64_t Id = 0;
    Vec3 Pos;
    std::vector<std::string> UsageTags;
    RiskTier Tier = RiskTier::R1;
    int64_t BuildingId = 0;             // points sharing a BuildingId form one building (use unique ids for standalone points)
    LootSize Size = LootSize::Large;    // largest item size the point can host
};

struct LootTuning
{
    double CycleIntervalS = 300.0;          // 5 minutes
    double MinPlayerDistanceM = 100.0;
    double BuildingCooldownS = 45.0 * 60.0;
    double WeaponPointFraction = 0.15;      // max share of a building's points holding weapon piles
    int MaxSpawnsPerCycle = 1000000;
    // [tier R0..R4][rarity common..very_rare], percent weights. R1..R4 come from docs/03 section 5; R0 is hub-grade junk only.
    int RarityWeights[5][4] = {{100, 0, 0, 0}, {85, 14, 1, 0}, {60, 33, 6, 1}, {35, 45, 17, 3}, {15, 40, 35, 10}};
    double DefaultLifetimeS = 3600.0;
};

struct LootWorldSnapshot
{
    double NowS = 0;
    std::unordered_map<std::string, int> Counts;               // current world count per defId (ground + containers)
    std::unordered_map<int64_t, double> BuildingLastSpawnS;    // absent = never spawned
    std::unordered_set<int64_t> OccupiedPointIds;              // points already holding a pile
    std::unordered_map<int64_t, int> WeaponPilesByBuilding;    // existing weapon piles
    std::vector<Vec3> PlayerPositions;
    std::function<bool(const Vec3&)> IsVisibleToAnyPlayer;     // optional; empty = nothing visible
};

struct SpawnDecision
{
    std::string DefId;
    int64_t PointId = 0;
    int64_t BuildingId = 0;
    Rarity Rarity_ = Rarity::Common;
    bool IsWeapon = false;
};

// True when a restock cycle is due (never earlier than CycleIntervalS after the previous one).
bool CycleDue(double lastCycleS, double nowS, const LootTuning& tuning = {});

// Max weapon piles a building with `pointCount` points may hold: floor(fraction * count).
int WeaponCapForBuilding(int pointCount, const LootTuning& tuning = {});

// One CE cycle. Types with count < Min are restocked up to Nominal (never above it), one pile per point.
std::vector<SpawnDecision> RunCycle(const std::vector<LootTypeDef>& types, const std::vector<LootSpawnPoint>& points,
                                    const LootWorldSnapshot& world, const LootTuning& tuning, Rng& rng);

// Records the decisions in the snapshot (counts, occupied points, building cooldown, weapon piles).
void ApplyDecisions(const std::vector<SpawnDecision>& decisions, LootWorldSnapshot& world);

struct GroundItemRecord
{
    int64_t ItemId = 0;
    std::string DefId;
    double DroppedAtS = 0;
    bool OnGround = true;               // false = in a container, body or stash: never expires
};

// Ids of ground items older than their type's lifetime (unknown defs use DefaultLifetimeS).
std::vector<int64_t> ExpiredGroundItems(const std::vector<GroundItemRecord>& items, const std::vector<LootTypeDef>& types,
                                        double nowS, const LootTuning& tuning = {});
} // namespace Scavar
