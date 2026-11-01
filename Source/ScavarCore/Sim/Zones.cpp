#include "Zones.h"
#include <algorithm>
#include <cmath>

namespace Scavar
{
namespace
{
bool OnSegment(const Point2& a, const Point2& b, double x, double y)
{
    const double cross = (b.X - a.X) * (y - a.Y) - (b.Y - a.Y) * (x - a.X);
    if (std::fabs(cross) > 1e-9) return false;
    return x >= std::min(a.X, b.X) - 1e-9 && x <= std::max(a.X, b.X) + 1e-9 && y >= std::min(a.Y, b.Y) - 1e-9 && y <= std::max(a.Y, b.Y) + 1e-9;
}
} // namespace

std::vector<Point2> Zone::Rect(double x0, double y0, double x1, double y1)
{
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

bool Zone::Contains(double x, double y) const
{
    const size_t n = Polygon.size();
    if (n < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const Point2& a = Polygon[i];
        const Point2& b = Polygon[j];
        if (OnSegment(a, b, x, y)) return true;
        if (((a.Y > y) != (b.Y > y)) && (x < (b.X - a.X) * (y - a.Y) / (b.Y - a.Y) + a.X)) inside = !inside;
    }
    return inside;
}

bool ZoneSet::Add(const Zone& zone)
{
    if (zone.Polygon.size() < 3 || zone.Contamination < 0 || zone.Contamination > 3) return false;
    for (const auto& z : Zones) if (z.Id == zone.Id) return false;
    Zones.push_back(zone);
    return true;
}

std::vector<const Zone*> ZoneSet::At(double x, double y) const
{
    std::vector<const Zone*> hits;
    for (const auto& z : Zones) if (z.Contains(x, y)) hits.push_back(&z);
    std::sort(hits.begin(), hits.end(), [](const Zone* a, const Zone* b) { return a->Priority != b->Priority ? a->Priority > b->Priority : a->Id < b->Id; });
    return hits;
}

const Zone* ZoneSet::Best(ZoneKind kind, double x, double y) const
{
    const Zone* best = nullptr;
    for (const auto& z : Zones)
    {
        if (z.Kind != kind || !z.Contains(x, y)) continue;
        if (!best || z.Priority > best->Priority || (z.Priority == best->Priority && z.Id < best->Id)) best = &z;
    }
    return best;
}

RiskTier ZoneSet::TierAt(double x, double y, RiskTier fallback) const
{
    const Zone* z = Best(ZoneKind::RiskTier, x, y);
    return z ? z->Tier : fallback;
}

int ZoneSet::ContaminationAt(double x, double y) const
{
    int level = 0;
    for (const auto& z : Zones) if (z.Kind == ZoneKind::ContaminationZone && z.Contains(x, y)) level = std::max(level, z.Contamination);
    return level;
}
} // namespace Scavar
