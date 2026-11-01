#pragma once
// Survival, stamina, movement and treatment rules (SYS-02, SYS-07). Pure and deterministic.
//
// Ambiguities resolved here:
//  * Hunger and thirst damage do NOT stack: any of them at 0 deals one StarvationDps to the torso (death >= 10 min from full torso).
//  * HP regeneration runs on every zone only while: food > 40, water > 40, no bleeding anywhere, cold stage Normal, character alive.
//  * Bleeding drain goes through DamageZone, so a bleeding limb at 0 HP keeps hurting the torso via the 35% spill.
//  * Stamina regen at rest: 6/s when food and water > 20 else 1.5/s; walking regenerates at half rate; "Cool" or colder: -25%.
//  * Crouch/prone speed factors and the walking regen factor are not in the spec: they are tuning values of this module.
//  * Treatments take effect only when the session completes; interrupted sessions have no effect and must not consume the item.
//  * Auto medkit pool is spent on zones in the order head, torso, legs, arms.
#include "Damage.h"

namespace Scavar
{
enum class MoveMode : uint8_t { Rest = 0, Walk, Jog, Sprint };
enum class Stance : uint8_t { Standing = 0, Crouch, Prone };
enum class ColdStage : uint8_t { Normal = 0, Cool, Cold, Hypothermia };

struct SurvivalState
{
    float Food = 100.f;
    float Water = 100.f;
    float Warmth = 100.f;   // 100 = comfortable. Written by the (future) environment model.
    float Wetness = 0.f;
};

struct StaminaState
{
    float Value = 100.f;
};

ColdStage ColdStageFor(float warmth, const CombatTuning& t = DefaultCombatTuning());
// Food/water decay (online time only; caller does not tick offline players).
void TickSurvival(SurvivalState& s, double dt, const CombatTuning& t = DefaultCombatTuning());
// Bleeding, starvation, hypothermia, regeneration, analgesia timer.
void TickHealth(CharacterHealth& h, const SurvivalState& s, double dt, const CombatTuning& t = DefaultCombatTuning());

bool CanSprint(const StaminaState& st, float carriedKg, const CombatTuning& t = DefaultCombatTuning());
// Effective speed in m/s for the requested mode (sprint falls back to jog when impossible); 0 when immobile (> 55 kg).
float MoveSpeed(MoveMode mode, Stance stance, float carriedKg, bool legFractured, bool staminaAllowsSprint, const CombatTuning& t = DefaultCombatTuning());
// `mode` is the mode actually performed this tick.
void TickStamina(StaminaState& st, MoveMode mode, const SurvivalState& s, double dt, const CombatTuning& t = DefaultCombatTuning());

// Aim sway multiplier: fractured arm x2, cold/hypothermia x1.5 (multiplicative).
float AimSwayMultiplier(const CharacterHealth& h, const SurvivalState& s, const CombatTuning& t = DefaultCombatTuning());

// ---- Treatment (SYS-07) ----
enum class TreatmentKind : uint8_t { Bandage = 0, Tourniquet, HemostaticPack, Splint, Analgesic, AutoMedKit };
enum class TreatmentStatus : uint8_t { InProgress = 0, Completed, Interrupted, Invalid };
enum class TreatmentInterrupt : uint8_t { DamageTaken, WeaponSwapped, SprintStarted };

float TreatmentDuration(TreatmentKind k, const CombatTuning& t = DefaultCombatTuning());
// Bandage: light bleed in zone. Tourniquet: any bleed on a limb. Hemostatic: any bleed in any zone.
// Splint: fractured limb. Analgesic: always. AutoMedKit: any missing HP.
bool CanTreat(TreatmentKind k, const CharacterHealth& h, HitZone zone, const CombatTuning& t = DefaultCombatTuning());
bool ApplyTreatment(TreatmentKind k, CharacterHealth& h, HitZone zone, const CombatTuning& t = DefaultCombatTuning());

struct TreatmentSession
{
    TreatmentKind Kind = TreatmentKind::Bandage;
    HitZone Zone = HitZone::Torso;
    float Elapsed = 0.f;
    float Duration = 0.f;
    TreatmentStatus Status = TreatmentStatus::InProgress;
};

TreatmentSession BeginTreatment(TreatmentKind k, HitZone zone, const CombatTuning& t = DefaultCombatTuning());
// On Completed the effect has been applied exactly once; the caller then consumes the item.
TreatmentStatus TickTreatment(TreatmentSession& s, double dt, CharacterHealth& h, const CombatTuning& t = DefaultCombatTuning());
void InterruptTreatment(TreatmentSession& s, TreatmentInterrupt reason);
} // namespace Scavar
