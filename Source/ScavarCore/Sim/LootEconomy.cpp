#include "LootEconomy.h"
#include <algorithm>

namespace Scavar
{
namespace
{
bool TagsMatch(const LootTypeDef& type, const LootSpawnPoint& point)
{
    if (type.UsageTags.empty()) return true;
    for (const auto& a : type.UsageTags)
        for (const auto& b : point.UsageTags)
            if (a == b) return true;
    return false;
}
} // namespace

bool CycleDue(double lastCycleS, double nowS, const LootTuning& tuning) { return nowS - lastCycleS >= tuning.CycleIntervalS; }

int WeaponCapForBuilding(int pointCount, const LootTuning& tuning)
{
    return static_cast<int>(std::floor(tuning.WeaponPointFraction * pointCount + 1e-9));
}

std::vector<SpawnDecision> RunCycle(const std::vector<LootTypeDef>& types, const std::vector<LootSpawnPoint>& points,
                                    const LootWorldSnapshot& world, const LootTuning& tuning, Rng& rng)
{
    std::vector<SpawnDecision> out;

    std::unordered_map<std::string, int> counts = world.Counts;
    std::vector<char> active(types.size(), 0);
    bool anyActive = false;
    for (size_t i = 0; i < types.size(); ++i)
    {
        auto it = counts.find(types[i].DefId);
        const int have = it == counts.end() ? 0 : it->second;
        if (types[i].Nominal > 0 && have < types[i].Min && have < types[i].Nominal) { active[i] = 1; anyActive = true; }
    }
    if (!anyActive) return out;

    std::unordered_map<int64_t, int> pointsPerBuilding;
    for (const auto& p : points) ++pointsPerBuilding[p.BuildingId];
    std::unordered_map<int64_t, int> weaponPiles = world.WeaponPilesByBuilding;

    std::vector<size_t> eligible;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const LootSpawnPoint& p = points[i];
        if (world.OccupiedPointIds.count(p.Id)) continue;
        auto last = world.BuildingLastSpawnS.find(p.BuildingId);
        if (last != world.BuildingLastSpawnS.end() && world.NowS - last->second < tuning.BuildingCooldownS) continue;
        bool nearPlayer = false;
        for (const Vec3& pl : world.PlayerPositions)
            if ((pl - p.Pos).Length() < tuning.MinPlayerDistanceM) { nearPlayer = true; break; }
        if (nearPlayer) continue;
        if (world.IsVisibleToAnyPlayer && world.IsVisibleToAnyPlayer(p.Pos)) continue;
        eligible.push_back(i);
    }

    for (size_t i = eligible.size(); i > 1; --i) std::swap(eligible[i - 1], eligible[rng.NextU64() % i]);

    std::vector<size_t> candidates;
    std::vector<int> weights;
    for (const size_t pi : eligible)
    {
        if (static_cast<int>(out.size()) >= tuning.MaxSpawnsPerCycle) break;
        const LootSpawnPoint& p = points[pi];
        const int tier = static_cast<int>(p.Tier);
        const int weaponCap = WeaponCapForBuilding(pointsPerBuilding[p.BuildingId], tuning);

        candidates.clear();
        weights.clear();
        int total = 0;
        for (size_t ti = 0; ti < types.size(); ++ti)
        {
            if (!active[ti]) continue;
            const LootTypeDef& t = types[ti];
            if (!(t.AllowedTiers & TierBit(p.Tier)) || t.Size > p.Size || !TagsMatch(t, p)) continue;
            if (t.IsWeapon && weaponPiles[p.BuildingId] >= weaponCap) continue;
            const int w = tuning.RarityWeights[tier][static_cast<int>(t.Rarity_)];
            if (w <= 0) continue;
            candidates.push_back(ti);
            weights.push_back(w);
            total += w;
        }
        if (candidates.empty()) continue;

        int roll = rng.RangeInt(0, total - 1), pick = 0;
        while (roll >= weights[static_cast<size_t>(pick)]) { roll -= weights[static_cast<size_t>(pick)]; ++pick; }
        const size_t ti = candidates[static_cast<size_t>(pick)];
        const LootTypeDef& t = types[ti];

        out.push_back({t.DefId, p.Id, p.BuildingId, t.Rarity_, t.IsWeapon});
        if (t.IsWeapon) ++weaponPiles[p.BuildingId];
        if (++counts[t.DefId] >= t.Nominal) active[ti] = 0;
        bool stillActive = false;
        for (char a : active) stillActive = stillActive || a;
        if (!stillActive) break;
    }
    return out;
}

void ApplyDecisions(const std::vector<SpawnDecision>& decisions, LootWorldSnapshot& world)
{
    for (const auto& d : decisions)
    {
        ++world.Counts[d.DefId];
        world.OccupiedPointIds.insert(d.PointId);
        world.BuildingLastSpawnS[d.BuildingId] = world.NowS;
        if (d.IsWeapon) ++world.WeaponPilesByBuilding[d.BuildingId];
    }
}

std::vector<int64_t> ExpiredGroundItems(const std::vector<GroundItemRecord>& items, const std::vector<LootTypeDef>& types,
                                        double nowS, const LootTuning& tuning)
{
    std::unordered_map<std::string, double> lifetime;
    for (const auto& t : types) lifetime[t.DefId] = t.LifetimeS;
    std::vector<int64_t> expired;
    for (const auto& it : items)
    {
        if (!it.OnGround) continue;
        auto l = lifetime.find(it.DefId);
        const double life = l == lifetime.end() ? tuning.DefaultLifetimeS : l->second;
        if (nowS - it.DroppedAtS >= life) expired.push_back(it.ItemId);
    }
    return expired;
}
} // namespace Scavar
