#pragma once
// SYS-08 rules 5-6: spawn groups around players, never in view, with caps and cooldowns. Pure logic.
#include "Ai.h"
#include "Rng.h"
#include "Types.h"
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace Scavar
{
struct AiGroupTemplate
{
    AiArchetype Archetype = AiArchetype::Marauder;
    int MinSize = 1, MaxSize = 1;
};

struct AiZone
{
    int64_t Id = 0;
    int MaxGroups = 1;
    std::vector<AiGroupTemplate> Templates;
    std::vector<Vec3> SpawnPoints;        // candidate hidden spawn locations inside the zone
    double CooldownMinS = 1800.0;         // after a group is KILLED the zone waits 30..60 min
    double CooldownMaxS = 3600.0;
};

struct DirectorTuning
{
    double ActivationRadiusM = 400.0;     // a player must be this close to a spawn point
    double MinPlayerDistanceM = 150.0;    // nobody closer than this
    double DespawnRadiusM = 600.0;
    double DespawnAfterS = 120.0;
    int GlobalNpcCap = 80;
};

struct ActiveGroup
{
    int64_t Id = 0;
    int64_t ZoneId = 0;
    AiArchetype Archetype = AiArchetype::Marauder;
    int Size = 1;
    Vec3 Pos;                             // the engine keeps this current
    double OutOfRangeSince = -1.0;
};

struct DirectorState
{
    std::vector<ActiveGroup> Groups;
    std::map<int64_t, double> ZoneNextSpawn;  // earliest time a zone may spawn again
    int64_t NextGroupId = 1;
    int ActiveNpcCount() const;
};

struct DirectorWorld
{
    double Now = 0;
    std::vector<Vec3> Players;
    // Must return true when any player could see the point. Empty == treated as visible (never spawn): safe default.
    std::function<bool(const Vec3&)> VisibleToAnyPlayer;
};

struct SpawnOrder
{
    int64_t GroupId = 0, ZoneId = 0;
    AiArchetype Archetype = AiArchetype::Marauder;
    int Size = 1;
    Vec3 Pos;
};

struct DirectorOutput
{
    std::vector<SpawnOrder> Spawns;
    std::vector<int64_t> Despawns;
};

DirectorOutput DirectorTick(DirectorState& state, const std::vector<AiZone>& zones, const DirectorWorld& world,
                            const DirectorTuning& tuning, Rng& rng);

// A group was killed by players: removes it and starts the zone cooldown.
void DirectorGroupKilled(DirectorState& state, const std::vector<AiZone>& zones, int64_t groupId, double now, Rng& rng);
} // namespace Scavar
