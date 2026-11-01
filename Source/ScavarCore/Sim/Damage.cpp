#include "Damage.h"
#include <algorithm>

namespace Scavar
{
const CombatTuning& DefaultCombatTuning()
{
    static const CombatTuning Tuning;
    return Tuning;
}

CharacterHealth MakeHealth(const CombatTuning& t)
{
    CharacterHealth h;
    h.Hp = t.ZoneMaxHp;
    h.Bleed.fill(BleedLevel::None);
    h.Fracture.fill(false);
    return h;
}

bool AnyBleeding(const CharacterHealth& h)
{
    for (BleedLevel b : h.Bleed) if (b != BleedLevel::None) return true;
    return false;
}

bool IsInPain(const CharacterHealth& h)
{
    if (h.AnalgesiaSeconds > 0.f) return false;
    for (int i = 0; i < ZoneCount; ++i)
    {
        const HitZone z = static_cast<HitZone>(i);
        if (h.Fracture[i] || h.Bleed[i] == BleedLevel::Heavy) return true;
        if (IsLimb(z) && h.Hp[i] <= 0.f) return true;
    }
    return false;
}

float TotalHp(const CharacterHealth& h)
{
    float sum = 0.f;
    for (float v : h.Hp) sum += v;
    return sum;
}

ArmorPiece MakeArmorPiece(const ArmorDef& def)
{
    ArmorPiece p;
    p.Present = true;
    p.ArmorClass = def.ArmorClass;
    p.Durability = def.Durability;
    p.MaxDurability = def.Durability;
    return p;
}

bool ArmorIsEffective(const ArmorPiece& p) { return p.Present && p.Durability > 0.f; }

int EffectiveArmorClass(const ArmorPiece& p, const CombatTuning& t)
{
    int cls = p.ArmorClass;
    if (p.MaxDurability > 0.f && p.Durability < p.MaxDurability * t.ArmorDegradedFraction) --cls;
    return cls < 0 ? 0 : cls;
}

float PenetrationChance(int penTier, int armorClass, const CombatTuning& t)
{
    return Clamp(t.PenBaseChance + t.PenChancePerTier * static_cast<float>(penTier - armorClass), t.PenChanceMin, t.PenChanceMax);
}

namespace
{
ArmorPiece* PieceFor(ArmorLoadout& a, HitZone z)
{
    if (z == HitZone::Head) return &a.Helmet;
    if (z == HitZone::Torso) return &a.Vest;
    return nullptr;
}

void UpdateDeath(CharacterHealth& h)
{
    if (h.Hp[ZoneIndex(HitZone::Head)] <= 0.f || h.Hp[ZoneIndex(HitZone::Torso)] <= 0.f) h.Dead = true;
}
} // namespace

float DamageZone(CharacterHealth& h, HitZone zone, float amount, const CombatTuning& t)
{
    if (amount <= 0.f || h.Dead) return 0.f;
    float& hp = h.Hp[ZoneIndex(zone)];
    float excess = 0.f;
    if (amount > hp) { excess = amount - hp; hp = 0.f; }
    else hp -= amount;

    float spill = 0.f;
    if (IsLimb(zone) && excess > 0.f)
    {
        spill = excess * t.LimbSpillToTorso;
        float& torso = h.Hp[ZoneIndex(HitZone::Torso)];
        torso = std::max(0.f, torso - spill);
    }
    UpdateDeath(h);
    return spill;
}

HitResult ApplyHit(CharacterHealth& h, ArmorLoadout& armor, const ProjectileHit& hit, Rng& rng, const CombatTuning& t)
{
    HitResult r;
    if (h.Dead) { r.Ignored = true; return r; }

    const int zi = ZoneIndex(hit.Zone);
    float dmg = hit.Damage * t.ZoneDamageMult[zi];

    ArmorPiece* piece = PieceFor(armor, hit.Zone);
    if (piece && ArmorIsEffective(*piece))
    {
        r.ArmorHit = true;
        const int eff = EffectiveArmorClass(*piece, t);
        r.Penetrated = rng.Chance(PenetrationChance(hit.PenTier, eff, t));
        if (r.Penetrated)
        {
            dmg *= std::max(0.f, 1.f - t.PenDamageReductionPerClass * static_cast<float>(eff));
            r.ArmorWear = t.ArmorWearPenetrated;
        }
        else
        {
            dmg *= t.BluntDamageFactor;
            r.Blunt = true;
            r.ArmorWear = t.ArmorWearBlunt;
        }
        piece->Durability = std::max(0.f, piece->Durability - r.ArmorWear);
    }

    r.DamageDealt = dmg;
    r.SpillToTorso = DamageZone(h, hit.Zone, dmg, t);

    if (!r.Blunt)
    {
        const bool heavyEligible = dmg >= t.HeavyBleedMinDamage || r.Penetrated;
        BleedLevel got = BleedLevel::None;
        if (heavyEligible && rng.Chance(t.HeavyBleedChance)) got = BleedLevel::Heavy;
        else if (dmg >= t.LightBleedMinDamage && rng.Chance(t.LightBleedChance)) got = BleedLevel::Light;
        if (got > h.Bleed[zi]) { h.Bleed[zi] = got; r.NewBleed = got; }
    }

    if (IsLimb(hit.Zone) && dmg >= t.FractureMinDamage && rng.Chance(t.FractureChance) && !h.Fracture[zi])
    {
        h.Fracture[zi] = true;
        r.NewFracture = true;
    }

    r.Killed = h.Dead;
    return r;
}

FallResult ResolveFall(CharacterHealth& h, float heightM, Rng& rng, const CombatTuning& t)
{
    FallResult r;
    if (heightM <= t.FallSafeHeightM || h.Dead) return r;

    r.LegDamageEach = (heightM - t.FallSafeHeightM) * t.FallDamagePerMeter * 0.5f;
    DamageZone(h, HitZone::LegL, r.LegDamageEach, t);
    DamageZone(h, HitZone::LegR, r.LegDamageEach, t);

    const bool certain = heightM > t.FallCertainFractureM;
    r.FracturedLegL = certain || rng.Chance(t.FallMidFractureChance);
    r.FracturedLegR = certain || rng.Chance(t.FallMidFractureChance);
    if (r.FracturedLegL) h.Fracture[ZoneIndex(HitZone::LegL)] = true;
    if (r.FracturedLegR) h.Fracture[ZoneIndex(HitZone::LegR)] = true;
    return r;
}

float BlastDamageAt(float d, const CombatTuning& t)
{
    if (d < 0.f) d = 0.f;
    if (d > t.BlastRadiusM) return 0.f;
    const float k = d / t.BlastRadiusM;
    return t.BlastDamageCenter + (t.BlastDamageEdge - t.BlastDamageCenter) * k;
}

std::vector<ProjectileHit> ExplosionFragments(float distanceM, Rng& rng, const CombatTuning& t)
{
    std::vector<ProjectileHit> out;
    const float total = BlastDamageAt(distanceM, t);
    if (total <= 0.f) return out;
    const int n = rng.RangeInt(t.FragmentsMin, t.FragmentsMax);
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        ProjectileHit h;
        h.Zone = static_cast<HitZone>(rng.RangeInt(0, ZoneCount - 1));
        h.Damage = total / static_cast<float>(n);
        h.PenTier = t.FragmentPenTier;
        out.push_back(h);
    }
    return out;
}
} // namespace Scavar
