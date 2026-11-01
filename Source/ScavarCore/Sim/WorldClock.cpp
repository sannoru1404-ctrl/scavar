#include "WorldClock.h"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Scavar
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double DegToRad = Pi / 180.0;

struct SeasonProfile
{
    double MeanC, AmplitudeC, DeclinationDeg;
    double WeatherWeight[5];   // clear, overcast, rain, fog, snow
};

const SeasonProfile& Profile(Season s)
{
    static const SeasonProfile profiles[4] = {
        { 7.0,  4.0,  -2.0, {30, 35, 20, 15,  0}},   // Autumn
        {-5.0,  3.0, -23.4, {20, 35,  0, 10, 35}},   // Winter
        {19.0,  6.0,  23.4, {55, 20, 20,  5,  0}},   // Summer
        { 8.0,  5.0,   0.0, {35, 30, 25, 10,  0}},   // Spring
    };
    return profiles[static_cast<int>(s)];
}

bool ParseDouble(const std::string& s, double& out) { auto r = std::from_chars(s.data(), s.data() + s.size(), out); return r.ec == std::errc() && r.ptr == s.data() + s.size(); }
bool ParseU64(const std::string& s, uint64_t& out) { auto r = std::from_chars(s.data(), s.data() + s.size(), out); return r.ec == std::errc() && r.ptr == s.data() + s.size(); }
std::string Num(double v) { char buf[48]; std::snprintf(buf, sizeof buf, "%.17g", v); return buf; }
} // namespace

const char* SeasonName(Season s)
{
    switch (s) { case Season::Autumn: return "Autumn"; case Season::Winter: return "Winter"; case Season::Summer: return "Summer"; case Season::Spring: return "Spring"; }
    return "?";
}

const char* WeatherName(Weather w)
{
    switch (w) { case Weather::Clear: return "Clear"; case Weather::Overcast: return "Overcast"; case Weather::Rain: return "Rain"; case Weather::Fog: return "Fog"; case Weather::Snow: return "Snow"; }
    return "?";
}

WeatherEffects EffectsFor(Weather w)
{
    switch (w)
    {
    case Weather::Clear:    return {1.00f, 0.00f, 0.000f,  0.f};
    case Weather::Overcast: return {0.90f, 0.00f, 0.000f, -1.f};
    case Weather::Rain:     return {0.70f, 0.25f, 0.010f, -2.f};
    case Weather::Fog:      return {0.40f, 0.02f, 0.002f, -1.f};
    case Weather::Snow:     return {0.60f, 0.05f, 0.015f, -2.f};
    }
    return {};
}

WorldClock::WorldClock(const WorldClockConfig& config) : Cfg(config), Random(config.Seed)
{
    if (Cfg.DayLengthMinutes < 1.0) Cfg.DayLengthMinutes = 1.0;
    if (Cfg.WeatherMaxRealMinutes < Cfg.WeatherMinRealMinutes) Cfg.WeatherMaxRealMinutes = Cfg.WeatherMinRealMinutes;
    Current = Weather::Clear;
    NextWeather();   // initial weather also comes from the seeded sequence
}

void WorldClock::NextWeather()
{
    const SeasonProfile& p = Profile(Cfg.SeasonPreset);
    double weights[5];
    double total = 0;
    for (int i = 0; i < 5; ++i)
    {
        weights[i] = (i == static_cast<int>(Current) && Draws > 0) ? 0.0 : p.WeatherWeight[i];
        total += weights[i];
    }
    const double pick = Random.NextUnit() * total;      // draw 1
    const double span = Random.NextUnit();              // draw 2
    double acc = 0;
    int chosen = 0;
    for (int i = 0; i < 5; ++i)
    {
        if (weights[i] <= 0.0) continue;
        chosen = i;
        acc += weights[i];
        if (pick < acc) break;
    }
    Current = static_cast<Weather>(chosen);
    WeatherLeftReal = (Cfg.WeatherMinRealMinutes + span * (Cfg.WeatherMaxRealMinutes - Cfg.WeatherMinRealMinutes)) * 60.0;
    ++Draws;
}

void WorldClock::Tick(double dt)
{
    if (!(dt > 0.0)) return;
    GameSeconds += dt * GameSecondsPerRealSecond();
    double remaining = dt;
    while (remaining >= WeatherLeftReal)
    {
        remaining -= WeatherLeftReal;
        NextWeather();
    }
    WeatherLeftReal -= remaining;
}

double WorldClock::HourOfDay() const
{
    const double seconds = std::fmod(Cfg.StartHour * 3600.0 + GameSeconds, 86400.0);
    return seconds / 3600.0;
}

int64_t WorldClock::DayIndex() const
{
    return static_cast<int64_t>(std::floor((Cfg.StartHour * 3600.0 + GameSeconds) / 86400.0));
}

double WorldClock::SunElevationDeg() const
{
    const double lat = Cfg.LatitudeDeg * DegToRad;
    const double dec = Profile(Cfg.SeasonPreset).DeclinationDeg * DegToRad;
    const double h = (HourOfDay() - 12.0) * 15.0 * DegToRad;
    const double s = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(h);
    return std::asin(s < -1.0 ? -1.0 : (s > 1.0 ? 1.0 : s)) / DegToRad;
}

double WorldClock::SunAzimuthDeg() const
{
    const double lat = Cfg.LatitudeDeg * DegToRad;
    const double dec = Profile(Cfg.SeasonPreset).DeclinationDeg * DegToRad;
    const double h = (HourOfDay() - 12.0) * 15.0 * DegToRad;
    const double elev = SunElevationDeg() * DegToRad;
    const double denom = std::cos(elev) * std::cos(lat);
    double cosAz = denom == 0.0 ? 1.0 : (std::sin(dec) - std::sin(elev) * std::sin(lat)) / denom;
    cosAz = cosAz < -1.0 ? -1.0 : (cosAz > 1.0 ? 1.0 : cosAz);
    double az = std::acos(cosAz) / DegToRad;   // 0..180 measured from north
    if (h > 0.0) az = 360.0 - az;              // afternoon: sun is west of south
    return az;
}

double WorldClock::AmbientTemperatureC() const
{
    const SeasonProfile& p = Profile(Cfg.SeasonPreset);
    const double daily = p.MeanC + p.AmplitudeC * std::cos(2.0 * Pi * (HourOfDay() - 15.0) / 24.0);
    return daily + EffectsFor(Current).TemperatureOffsetC;
}

std::string WorldClock::Serialize() const
{
    std::string s = "SCAVAR_CLOCK1";
    s += ";day_len=" + Num(Cfg.DayLengthMinutes);
    s += ";season=" + std::to_string(static_cast<int>(Cfg.SeasonPreset));
    s += ";start=" + Num(Cfg.StartHour);
    s += ";seed=" + std::to_string(Cfg.Seed);
    s += ";lat=" + Num(Cfg.LatitudeDeg);
    s += ";wmin=" + Num(Cfg.WeatherMinRealMinutes);
    s += ";wmax=" + Num(Cfg.WeatherMaxRealMinutes);
    s += ";game_s=" + Num(GameSeconds);
    s += ";weather=" + std::to_string(static_cast<int>(Current));
    s += ";left=" + Num(WeatherLeftReal);
    s += ";draws=" + std::to_string(Draws);
    return s;
}

bool WorldClock::Restore(const std::string& text, WorldClock& out)
{
    std::vector<std::pair<std::string, std::string>> kv;
    size_t pos = 0;
    bool first = true;
    while (pos <= text.size())
    {
        const size_t end = text.find(';', pos);
        const std::string part = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (first) { if (part != "SCAVAR_CLOCK1") return false; first = false; }
        else
        {
            const size_t eq = part.find('=');
            if (eq == std::string::npos) return false;
            kv.emplace_back(part.substr(0, eq), part.substr(eq + 1));
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    auto get = [&](const char* key, std::string& v) { for (const auto& p : kv) if (p.first == key) { v = p.second; return true; } return false; };

    WorldClockConfig cfg;
    double gameS = 0, left = 0, v = 0;
    uint64_t seed = 0, draws = 0, season = 0, weather = 0;
    std::string t;
    if (!get("day_len", t) || !ParseDouble(t, cfg.DayLengthMinutes)) return false;
    if (!get("season", t) || !ParseU64(t, season) || season > 3) return false;
    if (!get("start", t) || !ParseDouble(t, cfg.StartHour)) return false;
    if (!get("seed", t) || !ParseU64(t, seed)) return false;
    if (!get("lat", t) || !ParseDouble(t, cfg.LatitudeDeg)) return false;
    if (!get("wmin", t) || !ParseDouble(t, cfg.WeatherMinRealMinutes)) return false;
    if (!get("wmax", t) || !ParseDouble(t, cfg.WeatherMaxRealMinutes)) return false;
    if (!get("game_s", t) || !ParseDouble(t, gameS)) return false;
    if (!get("weather", t) || !ParseU64(t, weather) || weather > 4) return false;
    if (!get("left", t) || !ParseDouble(t, left)) return false;
    if (!get("draws", t) || !ParseU64(t, draws)) return false;
    (void)v;
    cfg.Seed = seed;
    cfg.SeasonPreset = static_cast<Season>(season);

    WorldClock restored(cfg);   // consumed 2 draws for the initial weather
    restored.Random.Seed(seed);
    for (uint64_t i = 0; i < draws * 2; ++i) restored.Random.NextUnit();
    restored.GameSeconds = gameS;
    restored.Current = static_cast<Weather>(weather);
    restored.WeatherLeftReal = left;
    restored.Draws = static_cast<uint32_t>(draws);
    out = restored;
    return true;
}
} // namespace Scavar
