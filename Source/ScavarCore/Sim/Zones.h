#pragma once
// SYS-11 rule 4: 2D volumes in the local metric world (metres) used by loot, AI, weapons and hazards.
#include "Types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Scavar
{
enum class ZoneKind : uint8_t { SafeZone = 0, RiskTier, AIZone, NoLootZone, ContaminationZone };

struct Point2 { double X = 0, Y = 0; };

struct Zone
{
    int64_t Id = 0;
    std::string Name;
    ZoneKind Kind = ZoneKind::RiskTier;
    RiskTier Tier = RiskTier::R1;     // RiskTier zones
    int Contamination = 0;            // ContaminationZone level 0-3
    int Priority = 0;                 // higher wins; ties: lower Id wins
    std::vector<Point2> Polygon;      // >= 3 vertices; an axis-aligned rectangle is just 4 vertices

    static std::vector<Point2> Rect(double x0, double y0, double x1, double y1);
    bool Contains(double x, double y) const;   // boundary counts as inside
};

class ZoneSet
{
public:
    // Returns false for invalid zones (fewer than 3 vertices, contamination outside 0..3) or duplicate ids.
    bool Add(const Zone& zone);
    size_t Size() const { return Zones.size(); }

    // All zones containing the point, best first (priority desc, id asc).
    std::vector<const Zone*> At(double x, double y) const;
    const Zone* Best(ZoneKind kind, double x, double y) const;

    bool IsSafeAt(double x, double y) const { return Best(ZoneKind::SafeZone, x, y) != nullptr; }
    bool WeaponsAllowedAt(double x, double y) const { return !IsSafeAt(x, y); }
    bool LootAllowedAt(double x, double y) const { return Best(ZoneKind::NoLootZone, x, y) == nullptr; }
    RiskTier TierAt(double x, double y, RiskTier fallback = RiskTier::R1) const;
    int ContaminationAt(double x, double y) const;   // highest level among overlapping zones

private:
    std::vector<Zone> Zones;
};
} // namespace Scavar
