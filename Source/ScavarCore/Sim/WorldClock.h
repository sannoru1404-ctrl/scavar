#pragma once
// SYS-11: one server clock for day/night, season preset and weather (AT-032).
// Time and weather are deterministic given (config, tick history); Serialize/Restore make them survive restarts.
#include <cstdint>
#include <string>
#include "Rng.h"

namespace Scavar
{
enum class Season : uint8_t { Autumn = 0, Winter, Summer, Spring };
enum class Weather : uint8_t { Clear = 0, Overcast, Rain, Fog, Snow };

struct WeatherEffects
{
    float Visibility = 1.f;        // multiplies AI/player sight distance
    float WetnessPerS = 0.f;       // wetness gained per second outdoors
    float CoolingPerS = 0.f;       // warmth lost per second outdoors (on top of temperature)
    float TemperatureOffsetC = 0.f;
};

struct WorldClockConfig
{
    double DayLengthMinutes = 240.0;   // real minutes per 24 game hours (ServerConfig.DayLengthMinutes)
    Season SeasonPreset = Season::Autumn;
    double StartHour = 8.0;            // game hour at server creation
    uint64_t Seed = 20011;
    double LatitudeDeg = 52.0;         // Polesie
    double WeatherMinRealMinutes = 15.0;
    double WeatherMaxRealMinutes = 45.0;
};

const char* SeasonName(Season s);
const char* WeatherName(Weather w);
WeatherEffects EffectsFor(Weather w);

class WorldClock
{
public:
    explicit WorldClock(const WorldClockConfig& config = {});

    void Tick(double dtRealSeconds);

    double GameSecondsTotal() const { return GameSeconds; }
    double HourOfDay() const;                  // [0,24)
    int64_t DayIndex() const;                  // whole game days since start (start day = 0)
    double SunElevationDeg() const;
    double SunAzimuthDeg() const;              // from north, clockwise
    bool IsNight() const { return SunElevationDeg() < -6.0; }
    double AmbientTemperatureC() const;        // season curve + weather offset

    Weather CurrentWeather() const { return Current; }
    double WeatherSecondsLeft() const { return WeatherLeftReal; }
    const WorldClockConfig& Config() const { return Cfg; }

    std::string Serialize() const;
    static bool Restore(const std::string& text, WorldClock& out);

private:
    void NextWeather();
    double GameSecondsPerRealSecond() const { return 86400.0 / (Cfg.DayLengthMinutes * 60.0); }

    WorldClockConfig Cfg;
    double GameSeconds = 0.0;
    Weather Current = Weather::Clear;
    double WeatherLeftReal = 0.0;
    uint32_t Draws = 0;     // weather transitions so far; each consumes exactly two Rng draws
    Rng Random;
};
} // namespace Scavar
