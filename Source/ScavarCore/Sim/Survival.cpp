#include "Survival.h"
#include <algorithm>

namespace Scavar
{
ColdStage ColdStageFor(float warmth, const CombatTuning& t)
{
    if (warmth < t.WarmthHypothermiaBelow) return ColdStage::Hypothermia;
    if (warmth < t.WarmthColdBelow) return ColdStage::Cold;
    if (warmth < t.WarmthCoolBelow) return ColdStage::Cool;
    return ColdStage::Normal;
}

void TickSurvival(SurvivalState& s, double dt, const CombatTuning& t)
{
    s.Food = std::max(0.f, s.Food - static_cast<float>(dt) * 100.f / t.FoodSecondsToEmpty);
    s.Water = std::max(0.f, s.Water - static_cast<float>(dt) * 100.f / t.WaterSecondsToEmpty);
}

void TickHealth(CharacterHealth& h, const SurvivalState& s, double dt, const CombatTuning& t)
{
    if (h.Dead) return;
    const float fdt = static_cast<float>(dt);
    h.AnalgesiaSeconds = std::max(0.f, h.AnalgesiaSeconds - fdt);

    bool bleeding = false;
    for (int i = 0; i < ZoneCount; ++i)
    {
        if (h.Bleed[i] == BleedLevel::None) continue;
        bleeding = true;
        const float dps = h.Bleed[i] == BleedLevel::Heavy ? t.HeavyBleedDps : t.LightBleedDps;
        DamageZone(h, static_cast<HitZone>(i), dps * fdt, t);
    }

    if (s.Food <= 0.f || s.Water <= 0.f) DamageZone(h, HitZone::Torso, t.StarvationDps * fdt, t);

    const ColdStage cold = ColdStageFor(s.Warmth, t);
    if (cold == ColdStage::Hypothermia) DamageZone(h, HitZone::Torso, t.HypothermiaDps * fdt, t);
    if (h.Dead) return;

    if (!bleeding && cold == ColdStage::Normal && s.Food > t.RegenMinFood && s.Water > t.RegenMinWater)
        for (int i = 0; i < ZoneCount; ++i)
            h.Hp[i] = std::min(t.ZoneMaxHp[i], h.Hp[i] + t.RegenHpPerSecond * fdt);
}

bool CanSprint(const StaminaState& st, float carriedKg, const CombatTuning& t)
{
    return st.Value > 0.f && carriedKg <= t.NoSprintKg;
}

float MoveSpeed(MoveMode mode, Stance stance, float carriedKg, bool legFractured, bool staminaAllowsSprint, const CombatTuning& t)
{
    if (carriedKg > t.ImmobileKg || mode == MoveMode::Rest) return 0.f;
    if (mode == MoveMode::Sprint && (!staminaAllowsSprint || carriedKg > t.NoSprintKg || stance != Stance::Standing)) mode = MoveMode::Jog;

    float v = mode == MoveMode::Walk ? t.SpeedWalk : mode == MoveMode::Jog ? t.SpeedJog : t.SpeedSprint;
    if (mode == MoveMode::Sprint && carriedKg > t.NoPenaltyKg) v *= t.HeavySprintFactor;
    if (stance == Stance::Crouch) v *= t.CrouchSpeedFactor;
    else if (stance == Stance::Prone) v *= t.ProneSpeedFactor;
    if (legFractured) v *= t.LegFractureSpeedFactor;
    return v;
}

void TickStamina(StaminaState& st, MoveMode mode, const SurvivalState& s, double dt, const CombatTuning& t)
{
    const float fdt = static_cast<float>(dt);
    if (mode == MoveMode::Sprint) st.Value -= t.SprintDrainPerS * fdt;
    else if (mode == MoveMode::Jog) st.Value -= t.JogDrainPerS * fdt;
    else
    {
        float regen = (s.Food > t.StaminaRegenMinNeeds && s.Water > t.StaminaRegenMinNeeds) ? t.RestRegenPerS : t.StarvedRegenPerS;
        if (mode == MoveMode::Walk) regen *= t.WalkRegenFactor;
        if (ColdStageFor(s.Warmth, t) != ColdStage::Normal) regen *= t.CoolStaminaRegenFactor;
        st.Value += regen * fdt;
    }
    st.Value = Clamp(st.Value, 0.f, t.StaminaMax);
}

float AimSwayMultiplier(const CharacterHealth& h, const SurvivalState& s, const CombatTuning& t)
{
    float m = 1.f;
    if (h.Fracture[ZoneIndex(HitZone::ArmL)] || h.Fracture[ZoneIndex(HitZone::ArmR)]) m *= t.AimSwayArmFracture;
    const ColdStage c = ColdStageFor(s.Warmth, t);
    if (c == ColdStage::Cold || c == ColdStage::Hypothermia) m *= t.AimSwayCold;
    return m;
}

float TreatmentDuration(TreatmentKind k, const CombatTuning& t)
{
    switch (k)
    {
    case TreatmentKind::Bandage: return t.TimeBandage;
    case TreatmentKind::Tourniquet: return t.TimeTourniquet;
    case TreatmentKind::HemostaticPack: return t.TimeHemostatic;
    case TreatmentKind::Splint: return t.TimeSplint;
    case TreatmentKind::Analgesic: return t.TimeAnalgesic;
    case TreatmentKind::AutoMedKit: return t.TimeAutoMedKit;
    }
    return 0.f;
}

bool CanTreat(TreatmentKind k, const CharacterHealth& h, HitZone zone, const CombatTuning& t)
{
    if (h.Dead) return false;
    const int zi = ZoneIndex(zone);
    switch (k)
    {
    case TreatmentKind::Bandage: return h.Bleed[zi] == BleedLevel::Light;
    case TreatmentKind::Tourniquet: return IsLimb(zone) && h.Bleed[zi] != BleedLevel::None;
    case TreatmentKind::HemostaticPack: return h.Bleed[zi] != BleedLevel::None;
    case TreatmentKind::Splint: return IsLimb(zone) && h.Fracture[zi];
    case TreatmentKind::Analgesic: return true;
    case TreatmentKind::AutoMedKit:
    {
        for (int i = 0; i < ZoneCount; ++i) if (h.Hp[i] < t.ZoneMaxHp[i]) return true;
        return false;
    }
    }
    return false;
}

bool ApplyTreatment(TreatmentKind k, CharacterHealth& h, HitZone zone, const CombatTuning& t)
{
    if (!CanTreat(k, h, zone, t)) return false;
    const int zi = ZoneIndex(zone);
    switch (k)
    {
    case TreatmentKind::Bandage:
    case TreatmentKind::Tourniquet:
    case TreatmentKind::HemostaticPack: h.Bleed[zi] = BleedLevel::None; break;
    case TreatmentKind::Splint: h.Fracture[zi] = false; break;
    case TreatmentKind::Analgesic: h.AnalgesiaSeconds = t.AnalgesiaSeconds; break;
    case TreatmentKind::AutoMedKit:
    {
        static const HitZone Order[ZoneCount] = {HitZone::Head, HitZone::Torso, HitZone::LegL, HitZone::LegR, HitZone::ArmL, HitZone::ArmR};
        float pool = t.AutoMedKitHealPool;
        for (HitZone z : Order)
        {
            const int i = ZoneIndex(z);
            const float add = std::min(pool, t.ZoneMaxHp[i] - h.Hp[i]);
            if (add > 0.f) { h.Hp[i] += add; pool -= add; }
        }
        break;
    }
    }
    return true;
}

TreatmentSession BeginTreatment(TreatmentKind k, HitZone zone, const CombatTuning& t)
{
    TreatmentSession s;
    s.Kind = k;
    s.Zone = zone;
    s.Duration = TreatmentDuration(k, t);
    return s;
}

TreatmentStatus TickTreatment(TreatmentSession& s, double dt, CharacterHealth& h, const CombatTuning& t)
{
    if (s.Status != TreatmentStatus::InProgress) return s.Status;
    s.Elapsed += static_cast<float>(dt);
    if (s.Elapsed < s.Duration) return s.Status;
    s.Status = ApplyTreatment(s.Kind, h, s.Zone, t) ? TreatmentStatus::Completed : TreatmentStatus::Invalid;
    return s.Status;
}

void InterruptTreatment(TreatmentSession& s, TreatmentInterrupt)
{
    if (s.Status == TreatmentStatus::InProgress) s.Status = TreatmentStatus::Interrupted;
}
} // namespace Scavar
