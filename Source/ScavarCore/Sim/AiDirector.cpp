#include "AiDirector.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Scavar
{
namespace
{
double NearestPlayer(const std::vector<Vec3>& players, const Vec3& p)
{
    double best = std::numeric_limits<double>::infinity();
    for (const Vec3& q : players) best = std::min(best, (q - p).Length());
    return best;
}

const AiZone* FindZone(const std::vector<AiZone>& zones, int64_t id)
{
    for (const auto& z : zones)
        if (z.Id == id) return &z;
    return nullptr;
}
} // namespace

int DirectorState::ActiveNpcCount() const
{
    int n = 0;
    for (const auto& g : Groups) n += g.Size;
    return n;
}

DirectorOutput DirectorTick(DirectorState& state, const std::vector<AiZone>& zones, const DirectorWorld& world,
                            const DirectorTuning& t, Rng& rng)
{
    DirectorOutput out;

    // 1. Despawn groups that nobody has been near for long enough (no cooldown: they were not killed).
    for (auto it = state.Groups.begin(); it != state.Groups.end();)
    {
        if (NearestPlayer(world.Players, it->Pos) > t.DespawnRadiusM)
        {
            if (it->OutOfRangeSince < 0) it->OutOfRangeSince = world.Now;
            if (world.Now - it->OutOfRangeSince >= t.DespawnAfterS)
            {
                out.Despawns.push_back(it->Id);
                it = state.Groups.erase(it);
                continue;
            }
        }
        else it->OutOfRangeSince = -1.0;
        ++it;
    }

    // 2. At most one new group per zone per tick.
    for (const AiZone& zone : zones)
    {
        if (zone.Templates.empty() || zone.SpawnPoints.empty()) continue;
        int inZone = 0;
        for (const auto& g : state.Groups) inZone += g.ZoneId == zone.Id ? 1 : 0;
        if (inZone >= zone.MaxGroups) continue;
        auto cd = state.ZoneNextSpawn.find(zone.Id);
        if (cd != state.ZoneNextSpawn.end() && world.Now < cd->second) continue;

        std::vector<const Vec3*> valid;
        for (const Vec3& p : zone.SpawnPoints)
        {
            const double d = NearestPlayer(world.Players, p);
            if (d > t.ActivationRadiusM || d < t.MinPlayerDistanceM) continue;
            if (!world.VisibleToAnyPlayer || world.VisibleToAnyPlayer(p)) continue;
            valid.push_back(&p);
        }
        if (valid.empty()) continue;

        const AiGroupTemplate& tpl = zone.Templates[static_cast<size_t>(rng.RangeInt(0, static_cast<int>(zone.Templates.size()) - 1))];
        const int size = rng.RangeInt(tpl.MinSize, std::max(tpl.MinSize, tpl.MaxSize));
        if (state.ActiveNpcCount() + size > t.GlobalNpcCap) continue;

        const Vec3 pos = *valid[static_cast<size_t>(rng.RangeInt(0, static_cast<int>(valid.size()) - 1))];
        ActiveGroup g;
        g.Id = state.NextGroupId++;
        g.ZoneId = zone.Id;
        g.Archetype = tpl.Archetype;
        g.Size = size;
        g.Pos = pos;
        state.Groups.push_back(g);
        out.Spawns.push_back(SpawnOrder{g.Id, zone.Id, tpl.Archetype, size, pos});
    }
    return out;
}

void DirectorGroupKilled(DirectorState& state, const std::vector<AiZone>& zones, int64_t groupId, double now, Rng& rng)
{
    for (auto it = state.Groups.begin(); it != state.Groups.end(); ++it)
    {
        if (it->Id != groupId) continue;
        if (const AiZone* z = FindZone(zones, it->ZoneId))
        {
            const double span = std::max(0.0, z->CooldownMaxS - z->CooldownMinS);
            state.ZoneNextSpawn[z->Id] = now + z->CooldownMinS + span * rng.NextUnit();
        }
        state.Groups.erase(it);
        return;
    }
}
} // namespace Scavar
