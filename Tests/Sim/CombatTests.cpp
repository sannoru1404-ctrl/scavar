#include "TestFramework.h"
#include "Ballistics.h"
#include "Damage.h"
#include "GameData.h"
#include "Survival.h"

using namespace Scavar;

namespace
{
const GameData& Data()
{
    static GameData d;
    static bool loaded = false;
    if (!loaded) { std::string err; loaded = d.Load(SCAVAR_DATA_DIR, &err); if (!loaded) std::printf("    data load error: %s\n", err.c_str()); }
    return d;
}

ArmorLoadout Naked() { return ArmorLoadout{}; }

HitResult Shoot(CharacterHealth& h, ArmorLoadout& a, HitZone z, const char* ammoId, Rng& rng)
{
    const AmmoDef* ammo = Data().FindAmmo(ammoId);
    ProjectileHit hit;
    hit.Zone = z; hit.Damage = ammo->Damage; hit.PenTier = ammo->PenTier;
    return ApplyHit(h, a, hit, rng);
}
} // namespace

// ---- SYS-06 formulas ----
TEST(Armor_PenetrationFormulaMatchesSpecTable)
{
    CHECK_NEAR(PenetrationChance(1, 3), 0.05, 1e-6);
    CHECK_NEAR(PenetrationChance(3, 2), 0.75, 1e-6);
    CHECK_NEAR(PenetrationChance(3, 3), 0.50, 1e-6);
    CHECK_NEAR(PenetrationChance(4, 4), 0.50, 1e-6);
    CHECK_NEAR(PenetrationChance(5, 4), 0.75, 1e-6);
    CHECK_NEAR(PenetrationChance(6, 1), 0.95, 1e-6);   // clamped
}

// AT-015: observed penetration rate over 10,000 hits within +-2% of the formula.
TEST(AT015_PenetrationRateMatchesFormula)
{
    struct Pair { const char* ammo; const char* armor; double expected; };
    const Pair pairs[] = {
        {"A_918_FMJ", "GEAR_VEST_6B3", 0.05},   // pen1 vs class3
        {"A_545_PS", "GEAR_VEST_ZH81", 0.75},   // pen3 vs class2
        {"A_545_PS", "GEAR_VEST_6B3", 0.50},    // pen3 vs class3
        {"A_762R_FMJ", "GEAR_VEST_6B5", 0.50},  // pen4 vs class4
        {"A_762R_AP", "GEAR_VEST_6B5", 0.75},   // pen5 vs class4
        {"A_12_BUCK", "GEAR_HELM_TANK", 0.50},  // pen1 vs class1 (head)
    };
    Rng rng(20011);
    for (const Pair& p : pairs)
    {
        const AmmoDef* ammo = Data().FindAmmo(p.ammo);
        const ArmorDef* def = Data().FindArmor(p.armor);
        CHECK(ammo && def);
        if (!ammo || !def) continue;
        const bool helmet = def->Slot == "helmet";
        int pen = 0;
        const int N = 10000;
        for (int i = 0; i < N; ++i)
        {
            CharacterHealth h = MakeHealth();
            ArmorLoadout a;
            (helmet ? a.Helmet : a.Vest) = MakeArmorPiece(*def);
            ProjectileHit hit{helmet ? HitZone::Head : HitZone::Torso, ammo->Damage, ammo->PenTier};
            if (ApplyHit(h, a, hit, rng).Penetrated) ++pen;
        }
        const double observed = static_cast<double>(pen) / N;
        if (std::fabs(observed - p.expected) > 0.02) std::printf("    %s vs %s observed %.4f expected %.2f\n", p.ammo, p.armor, observed, p.expected);
        CHECK_NEAR(observed, p.expected, 0.02);
    }
}

// AT-016
TEST(AT016_DeathAndLimbSpill)
{
    Rng rng(1);
    {   // torso zero -> dead
        CharacterHealth h = MakeHealth();
        DamageZone(h, HitZone::Torso, 1000.f);
        CHECK(h.Dead);
    }
    {   // head zero -> dead
        CharacterHealth h = MakeHealth();
        DamageZone(h, HitZone::Head, 35.f);
        CHECK(h.Dead);
    }
    {   // arm destroyed: excess 35% into torso, character alive
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        ProjectileHit hit{HitZone::ArmL, 200.f, 3};          // 200 * 0.7 = 140 vs 60 HP -> excess 80 -> spill 28
        HitResult r = ApplyHit(h, a, hit, rng);
        CHECK_NEAR(h.Hp[ZoneIndex(HitZone::ArmL)], 0.0, 1e-6);
        CHECK_NEAR(r.SpillToTorso, 28.0, 1e-4);
        CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 92.0, 1e-4);
        CHECK(!h.Dead);
        // already disabled limb: all damage is excess
        ProjectileHit again{HitZone::ArmL, 100.f, 3};        // 70 -> spill 24.5
        ApplyHit(h, a, again, rng);
        CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 92.0 - 24.5, 1e-3);
    }
    {   // dead characters ignore further hits
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        DamageZone(h, HitZone::Torso, 500.f);
        HitResult r = ApplyHit(h, a, ProjectileHit{HitZone::Head, 50.f, 3}, rng);
        CHECK(r.Ignored);
    }
}

// AT-017
TEST(AT017_ArmorWearAndDegradedClass)
{
    const ArmorDef* vest = Data().FindArmor("GEAR_VEST_6B3");   // class 3, durability 80
    CHECK(vest != nullptr);
    ArmorPiece p = MakeArmorPiece(*vest);
    CHECK_EQ(EffectiveArmorClass(p), 3);
    p.Durability = 40.f;                                        // exactly 50%: not degraded
    CHECK_EQ(EffectiveArmorClass(p), 3);
    p.Durability = 39.9f;
    CHECK_EQ(EffectiveArmorClass(p), 2);

    // wear amounts: 0.75 on penetration, 1.5 on blunt (find seeds producing each outcome)
    bool sawPen = false, sawBlunt = false;
    for (uint64_t seed = 1; seed < 200 && !(sawPen && sawBlunt); ++seed)
    {
        Rng rng(seed);
        CharacterHealth h = MakeHealth(); ArmorLoadout a; a.Vest = MakeArmorPiece(*vest);
        HitResult r = ApplyHit(h, a, ProjectileHit{HitZone::Torso, 45.f, 3}, rng);
        if (r.Penetrated) { sawPen = true; CHECK_NEAR(a.Vest.Durability, 80.0 - 0.75, 1e-4); }
        else { sawBlunt = true; CHECK(r.Blunt); CHECK_NEAR(a.Vest.Durability, 80.0 - 1.5, 1e-4); CHECK(r.NewBleed == BleedLevel::None); }
    }
    CHECK(sawPen && sawBlunt);

    // degraded armor penetrates more often (class 3 -> 2 against pen 3: 50% -> 75%)
    Rng rng(99); int fresh = 0, worn = 0; const int N = 4000;
    for (int i = 0; i < N; ++i)
    {
        CharacterHealth h = MakeHealth(); ArmorLoadout a; a.Vest = MakeArmorPiece(*vest);
        fresh += ApplyHit(h, a, ProjectileHit{HitZone::Torso, 45.f, 3}, rng).Penetrated ? 1 : 0;
        CharacterHealth h2 = MakeHealth(); ArmorLoadout b; b.Vest = MakeArmorPiece(*vest); b.Vest.Durability = 30.f;
        worn += ApplyHit(h2, b, ProjectileHit{HitZone::Torso, 45.f, 3}, rng).Penetrated ? 1 : 0;
    }
    CHECK_NEAR(static_cast<double>(fresh) / N, 0.50, 0.03);
    CHECK_NEAR(static_cast<double>(worn) / N, 0.75, 0.03);

    // destroyed armor gives no protection and takes no wear
    ArmorLoadout broken; broken.Vest = MakeArmorPiece(*vest); broken.Vest.Durability = 0.f;
    CharacterHealth h = MakeHealth(); Rng r2(5);
    HitResult res = ApplyHit(h, broken, ProjectileHit{HitZone::Torso, 45.f, 3}, r2);
    CHECK(!res.ArmorHit);
    CHECK_NEAR(res.DamageDealt, 45.0, 1e-6);
}

// AT-018
TEST(AT018_KillRulesForAssaultRifle)
{
    Rng rng(7);
    {   // 3 torso hits kill, 2 do not
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        Shoot(h, a, HitZone::Torso, "A_545_PS", rng);
        Shoot(h, a, HitZone::Torso, "A_545_PS", rng);
        CHECK(!h.Dead);
        HitResult r = Shoot(h, a, HitZone::Torso, "A_545_PS", rng);
        CHECK(h.Dead && r.Killed);
    }
    {   // 1 head hit kills
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        CHECK(Shoot(h, a, HitZone::Head, "A_545_PS", rng).Killed);
    }
    {   // helmet class 2 vs 9x18 FMJ: when the helmet holds, the hit is blunt and survivable
        const ArmorDef* helm = Data().FindArmor("GEAR_HELM_SSH68");
        CHECK(helm && helm->ArmorClass == 2);
        int held = 0, killed = 0;
        for (uint64_t seed = 1; seed <= 400; ++seed)
        {
            Rng r(seed);
            CharacterHealth h = MakeHealth(); ArmorLoadout a; a.Helmet = MakeArmorPiece(*helm);
            HitResult res = Shoot(h, a, HitZone::Head, "A_918_FMJ", r);
            if (!res.Penetrated) { ++held; CHECK(!h.Dead); CHECK_NEAR(res.DamageDealt, 28.0 * 2.0 * 0.25, 1e-4); }
            else if (h.Dead) ++killed;
        }
        CHECK(held > 200);        // ~75% hold
        CHECK(killed > 50);       // penetrations (25%) still kill: 28*2*0.9 = 50.4 >= 35
    }
}

TEST(Bleeding_RulesAndChances)
{
    // blunt never bleeds; penetrated hit can cause heavy bleeding; unarmored >=15 dmg can cause light
    Rng rng(321);
    int heavy = 0, light = 0; const int N = 20000;
    for (int i = 0; i < N; ++i)
    {
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        // 25 dmg to the arm: 17.5 final -> light-eligible only
        HitResult r = ApplyHit(h, a, ProjectileHit{HitZone::ArmL, 25.f, 1}, rng);
        if (r.NewBleed == BleedLevel::Light) ++light;
        if (r.NewBleed == BleedLevel::Heavy) ++heavy;
    }
    CHECK_EQ(heavy, 0);
    CHECK_NEAR(static_cast<double>(light) / N, 0.25, 0.015);

    heavy = 0;
    for (int i = 0; i < N; ++i)
    {
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        // 75 dmg torso -> final 75 (>=40): heavy 15%, else light 25% of remaining
        HitResult r = ApplyHit(h, a, ProjectileHit{HitZone::Torso, 75.f, 4}, rng);
        if (r.NewBleed == BleedLevel::Heavy) ++heavy;
    }
    CHECK_NEAR(static_cast<double>(heavy) / N, 0.15, 0.015);

    // below the light threshold: never bleeds (14 dmg pellet * 0.7 on a limb = 9.8)
    for (int i = 0; i < 2000; ++i)
    {
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        CHECK(ApplyHit(h, a, ProjectileHit{HitZone::LegL, 14.f, 1}, rng).NewBleed == BleedLevel::None);
    }
}

TEST(Fracture_OnlyLimbsAtHighDamage)
{
    Rng rng(11); int fractures = 0; const int N = 20000;
    for (int i = 0; i < N; ++i)
    {
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        if (ApplyHit(h, a, ProjectileHit{HitZone::LegR, 75.f, 4}, rng).NewFracture) ++fractures;   // 52.5 final
    }
    CHECK_NEAR(static_cast<double>(fractures) / N, 0.25, 0.015);
    for (int i = 0; i < 2000; ++i)
    {
        CharacterHealth h = MakeHealth(); ArmorLoadout a = Naked();
        CHECK(!ApplyHit(h, a, ProjectileHit{HitZone::LegR, 40.f, 4}, rng).NewFracture);            // 28 < 40
        CHECK(!ApplyHit(h, a, ProjectileHit{HitZone::Torso, 100.f, 4}, rng).NewFracture);          // torso never fractures
    }
}

// AT-019 and bleeding drain
TEST(AT019_BleedingTreatmentRules)
{
    CharacterHealth h = MakeHealth();
    h.Bleed[ZoneIndex(HitZone::LegL)] = BleedLevel::Heavy;
    h.Bleed[ZoneIndex(HitZone::Torso)] = BleedLevel::Light;
    h.Bleed[ZoneIndex(HitZone::Head)] = BleedLevel::Heavy;

    CHECK(!CanTreat(TreatmentKind::Bandage, h, HitZone::LegL));          // bandage cannot stop heavy
    CHECK(CanTreat(TreatmentKind::Bandage, h, HitZone::Torso));
    CHECK(CanTreat(TreatmentKind::Tourniquet, h, HitZone::LegL));
    CHECK(!CanTreat(TreatmentKind::Tourniquet, h, HitZone::Head));       // tourniquet is limbs only
    CHECK(!CanTreat(TreatmentKind::Tourniquet, h, HitZone::Torso));
    CHECK(CanTreat(TreatmentKind::HemostaticPack, h, HitZone::Head));    // pack works on any zone

    CHECK(ApplyTreatment(TreatmentKind::Bandage, h, HitZone::Torso));
    CHECK(h.Bleed[ZoneIndex(HitZone::Torso)] == BleedLevel::None);
    CHECK(ApplyTreatment(TreatmentKind::Tourniquet, h, HitZone::LegL));
    CHECK(ApplyTreatment(TreatmentKind::HemostaticPack, h, HitZone::Head));
    CHECK(!AnyBleeding(h));
    CHECK(!ApplyTreatment(TreatmentKind::Bandage, h, HitZone::Torso)); // nothing left to treat
}

TEST(Bleeding_DrainsWoundedZoneAndStopsRegen)
{
    CharacterHealth h = MakeHealth(); SurvivalState s;
    h.Bleed[ZoneIndex(HitZone::ArmR)] = BleedLevel::Light;
    h.Bleed[ZoneIndex(HitZone::LegR)] = BleedLevel::Heavy;
    for (int i = 0; i < 10; ++i) TickHealth(h, s, 1.0);
    CHECK_NEAR(h.Hp[ZoneIndex(HitZone::ArmR)], 60.0 - 3.0, 1e-3);
    CHECK_NEAR(h.Hp[ZoneIndex(HitZone::LegR)], 80.0 - 10.0, 1e-3);
    CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 120.0, 1e-6);   // no regen while bleeding
}

TEST(Regen_OnlyWhenFedWarmAndNotBleeding)
{
    CharacterHealth h = MakeHealth(); SurvivalState s;
    h.Hp[ZoneIndex(HitZone::Torso)] = 100.f;
    TickHealth(h, s, 100.0);
    CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 105.0, 1e-3);   // 0.05/s * 100 s
    s.Food = 39.f;
    TickHealth(h, s, 100.0);
    CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 105.0, 1e-3);
    s.Food = 100.f; s.Warmth = 50.f;                              // Cool
    TickHealth(h, s, 100.0);
    CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 105.0, 1e-3);
    s.Warmth = 100.f;
    TickHealth(h, s, 1000.0);
    CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 120.0, 1e-3);   // capped at max
}

TEST(AT020_InterruptedTreatmentHasNoEffect)
{
    CharacterHealth h = MakeHealth();
    h.Bleed[ZoneIndex(HitZone::Torso)] = BleedLevel::Light;
    TreatmentSession s = BeginTreatment(TreatmentKind::Bandage, HitZone::Torso);
    CHECK_NEAR(s.Duration, 4.0, 1e-6);
    CHECK(TickTreatment(s, 2.0, h) == TreatmentStatus::InProgress);
    InterruptTreatment(s, TreatmentInterrupt::DamageTaken);
    CHECK(TickTreatment(s, 10.0, h) == TreatmentStatus::Interrupted);
    CHECK(h.Bleed[ZoneIndex(HitZone::Torso)] == BleedLevel::Light);

    TreatmentSession ok = BeginTreatment(TreatmentKind::Bandage, HitZone::Torso);
    CHECK(TickTreatment(ok, 3.9, h) == TreatmentStatus::InProgress);
    CHECK(TickTreatment(ok, 0.2, h) == TreatmentStatus::Completed);
    CHECK(h.Bleed[ZoneIndex(HitZone::Torso)] == BleedLevel::None);
    CHECK(TickTreatment(ok, 1.0, h) == TreatmentStatus::Completed);    // effect applied exactly once

    CharacterHealth clean = MakeHealth();                                // nothing to treat at completion
    TreatmentSession bad = BeginTreatment(TreatmentKind::Splint, HitZone::LegL);
    CHECK(TickTreatment(bad, 9.0, clean) == TreatmentStatus::Invalid);
}

TEST(Treatment_SplintAnalgesicAndMedKit)
{
    CharacterHealth h = MakeHealth(); SurvivalState s;
    h.Fracture[ZoneIndex(HitZone::LegL)] = true;
    CHECK(IsInPain(h));
    CHECK(ApplyTreatment(TreatmentKind::Splint, h, HitZone::LegL));
    CHECK(!h.Fracture[ZoneIndex(HitZone::LegL)]);
    CHECK(!ApplyTreatment(TreatmentKind::Splint, h, HitZone::Torso));

    h.Fracture[ZoneIndex(HitZone::ArmL)] = true;
    CHECK(ApplyTreatment(TreatmentKind::Analgesic, h, HitZone::Torso));
    CHECK(!IsInPain(h));
    TickHealth(h, s, 181.0);
    CHECK(IsInPain(h));                                                  // 3 minutes later the fracture hurts again

    CharacterHealth w = MakeHealth();
    w.Hp[ZoneIndex(HitZone::Head)] = 5.f; w.Hp[ZoneIndex(HitZone::Torso)] = 20.f; w.Hp[ZoneIndex(HitZone::LegL)] = 10.f;
    const float before = TotalHp(w);
    CHECK(ApplyTreatment(TreatmentKind::AutoMedKit, w, HitZone::Torso));
    CHECK_NEAR(TotalHp(w) - before, 100.0, 1e-3);
    CHECK_NEAR(w.Hp[ZoneIndex(HitZone::Head)], 35.0, 1e-4);              // head and torso first
    CHECK_NEAR(w.Hp[ZoneIndex(HitZone::Torso)], 90.0, 1e-4);            // 100-pool: 30 to head, remaining 70 to torso
    CHECK_NEAR(w.Hp[ZoneIndex(HitZone::LegL)], 10.0, 1e-4);              // pool exhausted before limbs
    CharacterHealth full = MakeHealth();
    CHECK(!CanTreat(TreatmentKind::AutoMedKit, full, HitZone::Torso));
}

// AT-021
TEST(AT021_StarvationNeverKillsFasterThanTenMinutes)
{
    CharacterHealth h = MakeHealth(); SurvivalState s; s.Food = 0.f; s.Water = 0.f;
    double t = 0;
    while (!h.Dead && t < 5000) { TickHealth(h, s, 0.5); t += 0.5; }
    CHECK(h.Dead);
    CHECK(t >= 600.0);
    CHECK(t < 620.0);

    // decay rates: food empty after 4 h, water after 2.5 h
    SurvivalState d;
    for (int i = 0; i < 900; ++i) TickSurvival(d, 10.0);      // 9000 s = 2.5 h
    CHECK_NEAR(d.Water, 0.0, 0.05);
    CHECK(d.Food > 35.0 && d.Food < 40.0);
    for (int i = 0; i < 540; ++i) TickSurvival(d, 10.0);      // +5400 s = 4 h total
    CHECK_NEAR(d.Food, 0.0, 0.05);
}

TEST(Cold_StagesAndHypothermiaDamage)
{
    CHECK(ColdStageFor(80.f) == ColdStage::Normal);
    CHECK(ColdStageFor(59.9f) == ColdStage::Cool);
    CHECK(ColdStageFor(34.9f) == ColdStage::Cold);
    CHECK(ColdStageFor(14.9f) == ColdStage::Hypothermia);
    CharacterHealth h = MakeHealth(); SurvivalState s; s.Warmth = 5.f;
    TickHealth(h, s, 100.0);
    CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 120.0 - 10.0, 1e-3);
    CharacterHealth ok = MakeHealth(); SurvivalState cold; cold.Warmth = 30.f;
    cold.Food = 100.f;
    CHECK_NEAR(AimSwayMultiplier(ok, cold), 1.5, 1e-6);
    ok.Fracture[ZoneIndex(HitZone::ArmR)] = true;
    CHECK_NEAR(AimSwayMultiplier(ok, cold), 3.0, 1e-6);
}

// AT-005
TEST(AT005_StaminaAndMovement)
{
    SurvivalState s; StaminaState st;
    // sprint drains 9/s: empty after ~11.1 s, then sprint impossible
    double t = 0;
    while (CanSprint(st, 10.f) && t < 30) { TickStamina(st, MoveMode::Sprint, s, 0.1); t += 0.1; }
    CHECK_NEAR(t, 100.0 / 9.0, 0.2);
    CHECK_NEAR(st.Value, 0.0, 1e-6);
    CHECK(!CanSprint(st, 10.f));
    CHECK_NEAR(MoveSpeed(MoveMode::Sprint, Stance::Standing, 10.f, false, CanSprint(st, 10.f)), 3.8, 1e-6);   // falls back to jog

    // regen at rest 6/s (+-10%) with food and water > 20
    for (int i = 0; i < 50; ++i) TickStamina(st, MoveMode::Rest, s, 0.1);
    CHECK_NEAR(st.Value, 30.0, 3.0);
    // starved: slow regen; cool: -25%
    StaminaState a, b; a.Value = 0.f; b.Value = 0.f;
    SurvivalState hungry; hungry.Food = 10.f;
    SurvivalState cool; cool.Warmth = 50.f;
    TickStamina(a, MoveMode::Rest, hungry, 10.0);
    TickStamina(b, MoveMode::Rest, cool, 10.0);
    CHECK_NEAR(a.Value, 15.0, 1e-3);
    CHECK_NEAR(b.Value, 45.0, 1e-3);
    // jog drains 1/s
    StaminaState j; TickStamina(j, MoveMode::Jog, s, 10.0);
    CHECK_NEAR(j.Value, 90.0, 1e-4);

    // weight rules
    StaminaState full;
    CHECK_NEAR(MoveSpeed(MoveMode::Sprint, Stance::Standing, 20.f, false, true), 5.5, 1e-6);
    CHECK_NEAR(MoveSpeed(MoveMode::Sprint, Stance::Standing, 30.f, false, true), 5.5 * 0.75, 1e-6);
    CHECK_NEAR(MoveSpeed(MoveMode::Sprint, Stance::Standing, 41.f, false, true), 3.8, 1e-6);
    CHECK(!CanSprint(full, 41.f));
    CHECK_NEAR(MoveSpeed(MoveMode::Jog, Stance::Standing, 56.f, false, true), 0.0, 1e-9);
    CHECK_NEAR(MoveSpeed(MoveMode::Jog, Stance::Standing, 54.f, false, true), 3.8, 1e-6);
    CHECK_NEAR(MoveSpeed(MoveMode::Walk, Stance::Standing, 10.f, true, true), 1.6 * 0.6, 1e-6);   // fractured leg -40%
    CHECK_NEAR(MoveSpeed(MoveMode::Rest, Stance::Standing, 10.f, false, true), 0.0, 1e-9);
}

// AT-014 + ballistic sanity
TEST(AT014_BallisticsDeterministicAndDropSane)
{
    const AmmoDef* ammo = Data().FindAmmo("A_545_PS");
    CHECK(ammo != nullptr);
    const double v = ammo ? ammo->MuzzleVelocity : 890.0;
    ProjectileState reference;
    for (int shot = 0; shot < 1000; ++shot)
    {
        ProjectileState p = MakeProjectile(Vec3{0, 0, 1.6}, Vec3{1, 0, 0}, v);
        for (int i = 0; i < 120; ++i) StepProjectile(p, 1.0 / 60.0);
        if (shot == 0) reference = p;
        else CHECK(p.Pos.x == reference.Pos.x && p.Pos.y == reference.Pos.y && p.Pos.z == reference.Pos.z && p.Vel.z == reference.Vel.z);
    }
    // step size does not matter for constant gravity
    ProjectileState a = MakeProjectile(Vec3{0, 0, 1.6}, Vec3{1, 0, 0}, v), b = a;
    for (int i = 0; i < 60; ++i) StepProjectile(a, 1.0 / 60.0);
    for (int i = 0; i < 240; ++i) StepProjectile(b, 1.0 / 240.0);
    CHECK_NEAR(a.Pos.z, b.Pos.z, 1e-9);
    CHECK_NEAR(a.Pos.x, b.Pos.x, 1e-6);
    // drop at 300 m for 890 m/s is ~0.56 m
    const double drop = TrajectoryDropAt(890.0, 300.0);
    CHECK(drop > 0.5 && drop < 0.7);
    // stepping reproduces the analytic drop
    ProjectileState c = MakeProjectile(Vec3{0, 0, 0}, Vec3{1, 0, 0}, 890.0);
    const double flight = TimeOfFlight(890.0, 300.0);
    double elapsed = 0;
    while (elapsed + 1e-12 < flight) { double dt = std::min(1.0 / 60.0, flight - elapsed); StepProjectile(c, dt); elapsed += dt; }
    CHECK_NEAR(c.Pos.x, 300.0, 1e-6);
    CHECK_NEAR(-c.Pos.z, drop, 1e-9);
}

TEST(Material_ThinPassesWithPenaltyHardStops)
{
    MaterialResult w = ApplyMaterial(Material::ThinWood, 50.f, 3);
    CHECK(!w.Stopped); CHECK_NEAR(w.Damage, 30.0, 1e-4); CHECK_EQ(w.PenTier, 2);
    MaterialResult g = ApplyMaterial(Material::Glass, 20.f, 1);
    CHECK(!g.Stopped); CHECK_EQ(g.PenTier, 1);
    for (Material m : {Material::Metal, Material::Concrete, Material::Earth, Material::Water})
        CHECK(ApplyMaterial(m, 100.f, 6).Stopped);
}

TEST(Weapon_JamChanceBelowThirtyCondition)
{
    CHECK_NEAR(JamChance(100.f), 0.0, 1e-9);
    CHECK_NEAR(JamChance(30.f), 0.0, 1e-9);
    CHECK_NEAR(JamChance(29.99f), 0.05, 1e-3);
    CHECK_NEAR(JamChance(15.f), 0.10, 1e-6);
    CHECK_NEAR(JamChance(0.f), 0.15, 1e-6);
    Rng rng(3); int jams = 0; const int N = 20000;
    for (int i = 0; i < N; ++i) jams += RollJam(0.f, rng) ? 1 : 0;
    CHECK_NEAR(static_cast<double>(jams) / N, 0.15, 0.01);
    CHECK_NEAR(DefaultBallisticsTuning().JamClearSeconds, 2.5, 1e-9);
}

// AT-047
TEST(AT047_FallDamageThresholds)
{
    Rng rng(8);
    {   CharacterHealth h = MakeHealth(); FallResult r = ResolveFall(h, 3.0f, rng);
        CHECK_NEAR(r.LegDamageEach, 0.0, 1e-9); CHECK(!r.FracturedLegL && !r.FracturedLegR);
        CHECK_NEAR(TotalHp(h), TotalHp(MakeHealth()), 1e-6); }
    {   CharacterHealth h = MakeHealth(); ResolveFall(h, 1.0f, rng);
        CHECK_NEAR(TotalHp(h), TotalHp(MakeHealth()), 1e-6); }
    {   CharacterHealth h = MakeHealth(); FallResult r = ResolveFall(h, 4.0f, rng);   // (4-3)*20/2 = 10 per leg
        CHECK_NEAR(r.LegDamageEach, 10.0, 1e-4);
        CHECK_NEAR(h.Hp[ZoneIndex(HitZone::LegL)], 70.0, 1e-4);
        CHECK_NEAR(h.Hp[ZoneIndex(HitZone::Torso)], 120.0, 1e-6); }
    for (int i = 0; i < 200; ++i)   // above 5 m: both legs fractured every time
    {
        CharacterHealth h = MakeHealth(); FallResult r = ResolveFall(h, 5.5f, rng);
        CHECK(r.FracturedLegL && r.FracturedLegR);
        CHECK(h.Fracture[ZoneIndex(HitZone::LegL)] && h.Fracture[ZoneIndex(HitZone::LegR)]);
    }
    int fractured = 0;              // at exactly 5 m the fracture is NOT guaranteed
    for (int i = 0; i < 400; ++i) { CharacterHealth h = MakeHealth(); FallResult r = ResolveFall(h, 5.0f, rng); fractured += (r.FracturedLegL && r.FracturedLegR) ? 1 : 0; }
    CHECK(fractured < 400);
}

TEST(Grenade_BlastFalloffAndFragments)
{
    CHECK_NEAR(BlastDamageAt(0.f), 150.0, 1e-4);
    CHECK_NEAR(BlastDamageAt(6.f), 30.0, 1e-4);
    CHECK_NEAR(BlastDamageAt(3.f), 90.0, 1e-4);
    CHECK_NEAR(BlastDamageAt(6.01f), 0.0, 1e-9);
    Rng rng(4);
    bool saw4 = false, saw6 = false;
    for (int i = 0; i < 200; ++i)
    {
        auto frags = ExplosionFragments(2.f, rng);
        CHECK(frags.size() >= 4 && frags.size() <= 6);
        saw4 = saw4 || frags.size() == 4; saw6 = saw6 || frags.size() == 6;
        float sum = 0.f;
        for (const auto& f : frags) { CHECK_EQ(f.PenTier, 2); sum += f.Damage; }
        CHECK_NEAR(sum, BlastDamageAt(2.f), 1e-3);
    }
    CHECK(saw4 && saw6);
    CHECK(ExplosionFragments(7.f, rng).empty());
}
