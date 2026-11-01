#pragma once
// Engine-agnostic basic types shared by all Scavar simulation code.
// No Unreal headers here: this folder is compiled both by UE and by plain CMake.
#include <cstdint>
#include <cmath>

namespace Scavar
{
using ItemId = int64_t;
using ContainerId = int64_t;
using AccountId = int64_t;
constexpr int64_t InvalidId = 0;

enum class HitZone : uint8_t { Head = 0, Torso, ArmL, ArmR, LegL, LegR };
constexpr int ZoneCount = 6;
constexpr int ZoneIndex(HitZone z) { return static_cast<int>(z); }
constexpr bool IsLimb(HitZone z) { return z != HitZone::Head && z != HitZone::Torso; }
constexpr bool IsLeg(HitZone z) { return z == HitZone::LegL || z == HitZone::LegR; }

enum class Rarity : uint8_t { Common = 0, Uncommon, Rare, VeryRare };
enum class Release : uint8_t { Slice = 0, Alpha, EA };
enum class RiskTier : uint8_t { R0 = 0, R1, R2, R3, R4 };

struct Vec3
{
    double x = 0, y = 0, z = 0;
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double Length() const { return std::sqrt(x * x + y * y + z * z); }
    double Dist2D(const Vec3& o) const { return std::hypot(x - o.x, y - o.y); }
};

template <typename T> constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace Scavar
