#include "Ballistics.h"

namespace Scavar
{
const BallisticsTuning& DefaultBallisticsTuning()
{
    static const BallisticsTuning Tuning;
    return Tuning;
}

ProjectileState MakeProjectile(const Vec3& muzzle, const Vec3& direction, double speedMps)
{
    ProjectileState p;
    p.Pos = muzzle;
    const double len = direction.Length();
    p.Vel = len > 0.0 ? direction * (speedMps / len) : Vec3{};
    return p;
}

void StepProjectile(ProjectileState& p, double dt, const BallisticsTuning& t)
{
    p.Pos.x += p.Vel.x * dt;
    p.Pos.y += p.Vel.y * dt;
    p.Pos.z += p.Vel.z * dt - 0.5 * t.Gravity * dt * dt;
    p.Vel.z -= t.Gravity * dt;
    p.TimeAlive += dt;
}

double TimeOfFlight(double speedMps, double rangeM) { return speedMps > 0.0 ? rangeM / speedMps : 0.0; }

double TrajectoryDropAt(double speedMps, double rangeM, const BallisticsTuning& t)
{
    const double flight = TimeOfFlight(speedMps, rangeM);
    return 0.5 * t.Gravity * flight * flight;
}

MaterialResult ApplyMaterial(Material m, float damage, int penTier, const BallisticsTuning& t)
{
    MaterialResult r;
    if (m == Material::ThinWood || m == Material::Glass)
    {
        r.Damage = damage * t.ThinMaterialDamageMult;
        r.PenTier = penTier - t.ThinMaterialPenLoss < 1 ? 1 : penTier - t.ThinMaterialPenLoss;
        return r;
    }
    r.Stopped = true;
    return r;
}

float JamChance(float condition, const BallisticsTuning& t)
{
    if (condition >= t.JamBelowCondition) return 0.f;
    const float c = Clamp(condition, 0.f, t.JamBelowCondition);
    const float k = 1.f - c / t.JamBelowCondition;
    return t.JamChanceAtThreshold + (t.JamChanceAtZero - t.JamChanceAtThreshold) * k;
}

bool RollJam(float condition, Rng& rng, const BallisticsTuning& t)
{
    const float p = JamChance(condition, t);
    return p > 0.f && rng.Chance(p);
}
} // namespace Scavar
