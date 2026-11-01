#pragma once
// Combat damage model (docs/04_SYSTEMS_SPEC.md SYS-06). Pure, deterministic, engine-agnostic.
//
// Ambiguities resolved here (also see CombatTuning):
//  * Zone multiplier is applied to the raw projectile damage BEFORE armor. Pellets are separate hits.
//  * Armor covers Head (helmet) and Torso (vest) only. Armor with Durability <= 0 gives no protection.
//  * Effective class = class - 1 when Durability < 50% of max (class may therefore reach 0; the formula still holds).
//  * Penetrated: dmg *= (1 - 0.05*effClass), wear 0.75.  Not penetrated: dmg *= 0.25 ("blunt"), wear 1.5, NO bleeding.
//  * Heavy bleed is eligible when final damage >= 40 OR the hit penetrated armor; light bleed otherwise when final damage >= 15.
//    Heavy never gets downgraded by a later light roll. "Final damage" = damage after multiplier and armor, before HP clamp.
//  * Fracture: any limb, final damage >= 40, 25% (blunt hits included, they rarely reach 40).
//  * Limb at 0 HP is "disabled"; excess damage (including all damage on an already disabled limb) spills 35% into torso.
//  * Death: Head or Torso HP <= 0.
//  * RNG draw order inside ApplyHit is fixed: armor penetration, heavy bleed, light bleed, fracture (a draw happens only when that check is reached).
//  * Explosion: blast damage falls linearly from 150 (0 m) to 30 (6 m), is split equally across 4-6 fragments (pen 2) hitting uniformly random zones.
#include "GameData.h"
#include "Rng.h"
#include "Types.h"
#include <array>
#include <vector>

namespace Scavar
{
// Every tunable number of the combat/survival simulation lives here (values are TUNABLE v0, see spec).
struct CombatTuning
{
    // --- Zones (SYS-06) ---
    std::array<float, ZoneCount> ZoneMaxHp{35.f, 120.f, 60.f, 60.f, 80.f, 80.f};            // head, torso, armL, armR, legL, legR
    std::array<float, ZoneCount> ZoneDamageMult{2.0f, 1.0f, 0.7f, 0.7f, 0.7f, 0.7f};
    float LimbSpillToTorso = 0.35f;

    // --- Armor ---
    float PenBaseChance = 0.5f;
    float PenChancePerTier = 0.25f;
    float PenChanceMin = 0.05f;
    float PenChanceMax = 0.95f;
    float PenDamageReductionPerClass = 0.05f;
    float BluntDamageFactor = 0.25f;
    float ArmorWearPenetrated = 0.75f;
    float ArmorWearBlunt = 1.5f;
    float ArmorDegradedFraction = 0.5f;   // below this fraction of max durability the class drops by one

    // --- Bleeding / fracture ---
    float LightBleedMinDamage = 15.f;
    float LightBleedChance = 0.25f;
    float HeavyBleedMinDamage = 40.f;
    float HeavyBleedChance = 0.15f;
    float FractureMinDamage = 40.f;
    float FractureChance = 0.25f;
    float LightBleedDps = 0.3f;           // HP per second drained from the wounded zone
    float HeavyBleedDps = 1.0f;

    // --- Regeneration and survival (SYS-07) ---
    float RegenHpPerSecond = 0.05f;
    float RegenMinFood = 40.f;
    float RegenMinWater = 40.f;
    float FoodSecondsToEmpty = 4.f * 3600.f;
    float WaterSecondsToEmpty = 2.5f * 3600.f;
    float StarvationDps = 0.2f;           // to torso; thirst and hunger do NOT stack (death never faster than 10 min)
    float HypothermiaDps = 0.1f;
    float WarmthCoolBelow = 60.f;
    float WarmthColdBelow = 35.f;
    float WarmthHypothermiaBelow = 15.f;
    float AnalgesiaSeconds = 180.f;
    float AutoMedKitHealPool = 100.f;
    float AimSwayArmFracture = 2.0f;
    float AimSwayCold = 1.5f;

    // --- Treatment durations (seconds) ---
    float TimeBandage = 4.f, TimeTourniquet = 5.f, TimeHemostatic = 6.f, TimeSplint = 8.f, TimeAnalgesic = 2.f, TimeAutoMedKit = 10.f;

    // --- Movement and stamina (SYS-02) ---
    float StaminaMax = 100.f;
    float SprintDrainPerS = 9.f;
    float JogDrainPerS = 1.f;
    float RestRegenPerS = 6.f;
    float WalkRegenFactor = 0.5f;
    float StarvedRegenPerS = 1.5f;        // used when food or water <= StaminaRegenMinNeeds
    float StaminaRegenMinNeeds = 20.f;
    float CoolStaminaRegenFactor = 0.75f; // "Прохлада": regen -25%
    float SpeedWalk = 1.6f, SpeedJog = 3.8f, SpeedSprint = 5.5f;
    float CrouchSpeedFactor = 0.6f, ProneSpeedFactor = 0.25f;
    float LegFractureSpeedFactor = 0.6f;  // -40%
    float NoPenaltyKg = 25.f;
    float NoSprintKg = 40.f;
    float ImmobileKg = 55.f;
    float HeavySprintFactor = 0.75f;      // sprint -25% between 25 and 40 kg

    // --- Falls ---
    float FallSafeHeightM = 3.f;
    float FallCertainFractureM = 5.f;
    float FallDamagePerMeter = 20.f;      // total over safe height, split equally between both legs
    float FallMidFractureChance = 0.25f;  // per leg for 3 m < h <= 5 m

    // --- Explosion (hand grenade) ---
    float BlastRadiusM = 6.f;
    float BlastDamageCenter = 150.f;
    float BlastDamageEdge = 30.f;
    int FragmentsMin = 4, FragmentsMax = 6;
    int FragmentPenTier = 2;
};

const CombatTuning& DefaultCombatTuning();

enum class BleedLevel : uint8_t { None = 0, Light, Heavy };

struct CharacterHealth
{
    std::array<float, ZoneCount> Hp{};
    std::array<BleedLevel, ZoneCount> Bleed{};
    std::array<bool, ZoneCount> Fracture{};
    float AnalgesiaSeconds = 0.f;
    bool Dead = false;
};

CharacterHealth MakeHealth(const CombatTuning& t = DefaultCombatTuning());
bool AnyBleeding(const CharacterHealth& h);
bool IsInPain(const CharacterHealth& h);
float TotalHp(const CharacterHealth& h);

struct ArmorPiece
{
    bool Present = false;
    int ArmorClass = 0;
    float Durability = 0.f;
    float MaxDurability = 0.f;
};

struct ArmorLoadout
{
    ArmorPiece Helmet;
    ArmorPiece Vest;
};

ArmorPiece MakeArmorPiece(const ArmorDef& def);
bool ArmorIsEffective(const ArmorPiece& p);
int EffectiveArmorClass(const ArmorPiece& p, const CombatTuning& t = DefaultCombatTuning());
// P = clamp(0.5 + 0.25*(pen - class), 0.05, 0.95)
float PenetrationChance(int penTier, int armorClass, const CombatTuning& t = DefaultCombatTuning());

struct ProjectileHit
{
    HitZone Zone = HitZone::Torso;
    float Damage = 0.f;   // damage of ONE projectile/pellet before any multiplier
    int PenTier = 1;
};

struct HitResult
{
    bool Ignored = false;       // target already dead
    bool ArmorHit = false;      // an effective armor piece covered the zone
    bool Penetrated = false;    // armor was present and penetrated
    bool Blunt = false;         // armor present and not penetrated
    float DamageDealt = 0.f;    // final damage aimed at the zone (after multiplier and armor)
    float ArmorWear = 0.f;
    float SpillToTorso = 0.f;
    BleedLevel NewBleed = BleedLevel::None;
    bool NewFracture = false;
    bool Killed = false;
};

HitResult ApplyHit(CharacterHealth& health, ArmorLoadout& armor, const ProjectileHit& hit, Rng& rng, const CombatTuning& t = DefaultCombatTuning());

// Raw damage into a zone (no armor, no multiplier): clamps at 0, limb excess spills into torso, updates Dead.
// Returns the amount spilled into the torso.
float DamageZone(CharacterHealth& h, HitZone zone, float amount, const CombatTuning& t = DefaultCombatTuning());

// Falls (SYS-02): nothing up to FallSafeHeightM, leg damage above it, guaranteed leg fractures above FallCertainFractureM.
struct FallResult
{
    float LegDamageEach = 0.f;
    bool FracturedLegL = false, FracturedLegR = false;
};
FallResult ResolveFall(CharacterHealth& h, float heightM, Rng& rng, const CombatTuning& t = DefaultCombatTuning());

// Explosions
float BlastDamageAt(float distanceM, const CombatTuning& t = DefaultCombatTuning());
std::vector<ProjectileHit> ExplosionFragments(float distanceM, Rng& rng, const CombatTuning& t = DefaultCombatTuning());
} // namespace Scavar
