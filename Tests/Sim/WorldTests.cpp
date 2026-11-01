#include "TestFramework.h"
#include "WorldClock.h"
#include "Zones.h"
#include "GameData.h"
#include <set>

using namespace Scavar;

namespace
{
WorldClockConfig Cfg(Season s, uint64_t seed = 77)
{
    WorldClockConfig c;
    c.SeasonPreset = s;
    c.Seed = seed;
    return c;
}
double NightFraction(Season s)
{
    WorldClock c(Cfg(s));
    int night = 0;
    const int steps = 24 * 60;
    for (int i = 0; i < steps; ++i)
    {
        night += c.IsNight() ? 1 : 0;
        c.Tick(240.0 * 60.0 / steps);   // one game minute per step
    }
    return static_cast<double>(night) / steps;
}
}

TEST(Clock_TimeOfDayScalesWithDayLength)
{
    WorldClockConfig cfg = Cfg(Season::Autumn);
    cfg.StartHour = 8.0;
    WorldClock c(cfg);
    c.Tick(120.0 * 60.0);   // half of a 240-minute day => 12 game hours
    CHECK_NEAR(c.HourOfDay(), 20.0, 1e-6);
    CHECK_EQ(c.DayIndex(), int64_t(0));
    c.Tick(120.0 * 60.0);
    CHECK_NEAR(c.HourOfDay(), 8.0, 1e-6);
    CHECK_EQ(c.DayIndex(), int64_t(1));

    WorldClockConfig fast = cfg;
    fast.DayLengthMinutes = 60.0;
    WorldClock f(fast);
    f.Tick(30.0 * 60.0);
    CHECK_NEAR(f.HourOfDay(), 20.0, 1e-6);
}

TEST(Clock_SunGeometryIsPlausible)
{
    WorldClockConfig cfg = Cfg(Season::Autumn);
    cfg.StartHour = 12.0;
    WorldClock noon(cfg);
    CHECK_NEAR(noon.SunElevationDeg(), 90.0 - 52.0 - 2.0, 0.01);
    CHECK_NEAR(noon.SunAzimuthDeg(), 180.0, 0.5);
    CHECK(!noon.IsNight());

    cfg.StartHour = 0.0;
    WorldClock midnight(cfg);
    CHECK(midnight.SunElevationDeg() < 0.0);
    CHECK(midnight.IsNight());

    cfg.StartHour = 9.0;
    WorldClock morning(cfg);
    CHECK(morning.SunAzimuthDeg() > 90.0 && morning.SunAzimuthDeg() < 180.0);   // east of south
    cfg.StartHour = 15.0;
    WorldClock afternoon(cfg);
    CHECK(afternoon.SunAzimuthDeg() > 180.0 && afternoon.SunAzimuthDeg() < 270.0);
}

TEST(Clock_SeasonsChangeNightLengthAndTemperature)
{
    const double winter = NightFraction(Season::Winter);
    const double autumn = NightFraction(Season::Autumn);
    const double summer = NightFraction(Season::Summer);
    CHECK(winter > autumn && autumn > summer);
    CHECK(winter < 0.75 && summer > 0.15);

    WorldClockConfig w = Cfg(Season::Winter), s = Cfg(Season::Summer);
    w.StartHour = s.StartHour = 15.0;
    WorldClock cw(w), cs(s);
    CHECK(cs.AmbientTemperatureC() - cw.AmbientTemperatureC() > 15.0);
}

TEST(Clock_SnowOnlyInWinterAndRainNeverInWinter)
{
    for (Season season : {Season::Autumn, Season::Winter, Season::Summer, Season::Spring})
    {
        WorldClock c(Cfg(season, 123));
        std::set<Weather> seen;
        for (int i = 0; i < 4000; ++i) { seen.insert(c.CurrentWeather()); c.Tick(60.0); }
        if (season == Season::Winter) { CHECK(seen.count(Weather::Snow) == 1); CHECK(seen.count(Weather::Rain) == 0); }
        else CHECK(seen.count(Weather::Snow) == 0);
        CHECK(seen.size() >= 3);
    }
}

TEST(Clock_WeatherIsDeterministicAndChangesEveryTransition)
{
    WorldClock a(Cfg(Season::Autumn, 5)), b(Cfg(Season::Autumn, 5)), other(Cfg(Season::Autumn, 6));
    Weather previous = a.CurrentWeather();
    int changes = 0;
    bool differsFromOtherSeed = false;
    for (int i = 0; i < 5000; ++i)
    {
        a.Tick(30.0); b.Tick(30.0); other.Tick(30.0);
        CHECK(a.CurrentWeather() == b.CurrentWeather());
        differsFromOtherSeed = differsFromOtherSeed || a.CurrentWeather() != other.CurrentWeather();
        if (a.CurrentWeather() != previous) { ++changes; previous = a.CurrentWeather(); }
    }
    CHECK(changes > 20);
    CHECK(differsFromOtherSeed);
    const WeatherEffects fog = EffectsFor(Weather::Fog), clear = EffectsFor(Weather::Clear), rain = EffectsFor(Weather::Rain);
    CHECK(fog.Visibility < clear.Visibility);
    CHECK(rain.WetnessPerS > clear.WetnessPerS);
}

TEST(AT032_Clock_SerializeRestoreContinuesIdentically)
{
    WorldClock continuous(Cfg(Season::Spring, 99));
    WorldClock restarted(Cfg(Season::Spring, 99));
    for (int i = 0; i < 400; ++i) { continuous.Tick(7.3); restarted.Tick(7.3); }

    const std::string saved = restarted.Serialize();
    WorldClock afterRestart(Cfg(Season::Autumn, 1));   // deliberately different defaults: Restore must overwrite everything
    CHECK(WorldClock::Restore(saved, afterRestart));
    CHECK_EQ(afterRestart.Serialize(), saved);

    for (int i = 0; i < 900; ++i) { continuous.Tick(7.3); afterRestart.Tick(7.3); }
    CHECK_EQ(continuous.Serialize(), afterRestart.Serialize());
    CHECK_NEAR(continuous.HourOfDay(), afterRestart.HourOfDay(), 1e-9);
    CHECK(continuous.CurrentWeather() == afterRestart.CurrentWeather());
    CHECK_NEAR(continuous.AmbientTemperatureC(), afterRestart.AmbientTemperatureC(), 1e-9);
}

TEST(Clock_RestoreRejectsGarbage)
{
    WorldClock c(Cfg(Season::Autumn));
    CHECK(!WorldClock::Restore("", c));
    CHECK(!WorldClock::Restore("nonsense", c));
    CHECK(!WorldClock::Restore("SCAVAR_CLOCK1;day_len=240", c));
    std::string bad = c.Serialize();
    bad.replace(bad.find("season=0"), 8, "season=9");
    CHECK(!WorldClock::Restore(bad, c));
}

TEST(Zones_RectanglesPolygonsAndBoundaries)
{
    Zone rect; rect.Id = 1; rect.Kind = ZoneKind::SafeZone; rect.Polygon = Zone::Rect(0, 0, 100, 50);
    CHECK(rect.Contains(50, 25));
    CHECK(rect.Contains(0, 0) && rect.Contains(100, 50) && rect.Contains(100, 25));   // boundary is inside
    CHECK(!rect.Contains(100.01, 25) && !rect.Contains(-1, 10));

    Zone tri; tri.Id = 2; tri.Polygon = {{0, 0}, {10, 0}, {0, 10}};
    CHECK(tri.Contains(2, 2) && tri.Contains(5, 5));       // on the hypotenuse
    CHECK(!tri.Contains(6, 6));

    Zone concave; concave.Id = 3; concave.Polygon = {{0, 0}, {10, 0}, {10, 10}, {5, 4}, {0, 10}};
    CHECK(concave.Contains(2, 2));
    CHECK(!concave.Contains(5, 8));                         // inside the notch
}

TEST(Zones_PriorityTiesAndValidation)
{
    ZoneSet set;
    Zone r1; r1.Id = 1; r1.Kind = ZoneKind::RiskTier; r1.Tier = RiskTier::R1; r1.Priority = 0; r1.Polygon = Zone::Rect(0, 0, 1000, 1000);
    Zone r3; r3.Id = 2; r3.Kind = ZoneKind::RiskTier; r3.Tier = RiskTier::R3; r3.Priority = 5; r3.Polygon = Zone::Rect(400, 400, 600, 600);
    Zone tie; tie.Id = 3; tie.Kind = ZoneKind::RiskTier; tie.Tier = RiskTier::R2; tie.Priority = 5; tie.Polygon = Zone::Rect(400, 400, 600, 600);
    CHECK(set.Add(r1) && set.Add(r3) && set.Add(tie));
    CHECK(!set.Add(r1));                                    // duplicate id
    Zone bad; bad.Id = 9; bad.Polygon = {{0, 0}, {1, 1}};
    CHECK(!set.Add(bad));
    Zone badContam; badContam.Id = 10; badContam.Kind = ZoneKind::ContaminationZone; badContam.Contamination = 4; badContam.Polygon = Zone::Rect(0, 0, 1, 1);
    CHECK(!set.Add(badContam));

    CHECK(set.TierAt(500, 500) == RiskTier::R3);            // higher priority, tie -> lower id
    CHECK(set.TierAt(100, 100) == RiskTier::R1);
    CHECK(set.TierAt(5000, 5000, RiskTier::R2) == RiskTier::R2);   // fallback outside any zone
    CHECK_EQ(set.At(500, 500).size(), size_t(3));
    CHECK_EQ(set.At(500, 500).front()->Id, int64_t(2));
}

TEST(AT033_Zones_SafeZoneBlocksWeaponsAtCanonicalPoints)
{
    GameData game; std::string err;
    CHECK(game.Load(SCAVAR_DATA_DIR, &err));
    ZoneSet set;
    Zone hub; hub.Id = 1; hub.Name = "Berezovets hub"; hub.Kind = ZoneKind::SafeZone; hub.Priority = 10;
    hub.Polygon = Zone::Rect(4300, 2800, 5400, 4100);        // metres, covers POI_001..003 (R0)
    CHECK(set.Add(hub));
    Zone nolootHub; nolootHub.Id = 2; nolootHub.Kind = ZoneKind::NoLootZone; nolootHub.Polygon = hub.Polygon;
    CHECK(set.Add(nolootHub));
    Zone toxic1; toxic1.Id = 3; toxic1.Kind = ZoneKind::ContaminationZone; toxic1.Contamination = 1; toxic1.Polygon = Zone::Rect(7500, 4500, 8500, 5500);
    Zone toxic3; toxic3.Id = 4; toxic3.Kind = ZoneKind::ContaminationZone; toxic3.Contamination = 3; toxic3.Polygon = Zone::Rect(7900, 4900, 8100, 5100);
    CHECK(set.Add(toxic1) && set.Add(toxic3));

    for (const char* id : {"POI_001", "POI_002", "POI_003"})
    {
        const PoiDef* p = game.FindPoi(id);
        CHECK(p != nullptr);
        if (!p) continue;
        CHECK(!set.WeaponsAllowedAt(p->XKm * 1000, p->YKm * 1000));
        CHECK(!set.LootAllowedAt(p->XKm * 1000, p->YKm * 1000));
    }
    for (const char* id : {"POI_007", "POI_014", "POI_021"})
    {
        const PoiDef* p = game.FindPoi(id);
        CHECK(p != nullptr);
        if (p) CHECK(set.WeaponsAllowedAt(p->XKm * 1000, p->YKm * 1000));
    }
    const PoiDef* barracks = game.FindPoi("POI_021");
    CHECK(barracks != nullptr);
    if (barracks) CHECK_EQ(set.ContaminationAt(barracks->XKm * 1000, barracks->YKm * 1000), 3);   // highest overlapping level
    CHECK_EQ(set.ContaminationAt(7600, 4600), 1);
    CHECK_EQ(set.ContaminationAt(100, 100), 0);
}
