#pragma once
// Server-side projectile stepping (SYS-05, ADR-005): gravity only, no drag, time of flight matters.
// Coordinates: z is up. Gravity is constant, so the kinematic update below is exact for any dt.
#include "Damage.h"
#include "Rng.h"
#include "Types.h"

namespace Scavar
{
struct BallisticsTuning
{
    double Gravity = 9.81;                // m/s^2
    double StepSeconds = 1.0 / 60.0;
    float ThinMaterialDamageMult = 0.6f;  // thin wood / glass
    int ThinMaterialPenLoss = 1;
    float JamBelowCondition = 30.f;
    float JamChanceAtThreshold = 0.05f;   // at condition == 30
    float JamChanceAtZero = 0.15f;        // at condition == 0, linear in between
    float JamClearSeconds = 2.5f;
};
const BallisticsTuning& DefaultBallisticsTuning();

struct ProjectileState
{
    Vec3 Pos;
    Vec3 Vel;
    double TimeAlive = 0.0;
};

// `direction` need not be normalised.
ProjectileState MakeProjectile(const Vec3& muzzle, const Vec3& direction, double speedMps);
void StepProjectile(ProjectileState& p, double dt, const BallisticsTuning& t = DefaultBallisticsTuning());
// Drop (metres, positive downward) of a projectile fired perfectly horizontally after travelling `rangeM` horizontally.
double TrajectoryDropAt(double speedMps, double rangeM, const BallisticsTuning& t = DefaultBallisticsTuning());
double TimeOfFlight(double speedMps, double rangeM);

enum class Material : uint8_t { ThinWood, Glass, Metal, Concrete, Earth, Water };
struct MaterialResult
{
    bool Stopped = false;
    float Damage = 0.f;
    int PenTier = 0;
};
MaterialResult ApplyMaterial(Material m, float damage, int penTier, const BallisticsTuning& t = DefaultBallisticsTuning());

// Weapon condition 0..100: below 30 the weapon can jam (5% -> 15% as condition falls 30 -> 0).
float JamChance(float condition, const BallisticsTuning& t = DefaultBallisticsTuning());
bool RollJam(float condition, Rng& rng, const BallisticsTuning& t = DefaultBallisticsTuning());
} // namespace Scavar
