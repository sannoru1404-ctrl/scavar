#pragma once
// Held-weapon state machine (SYS-05, SYS-03 rule 7): fire modes, chamber, magazines, RPM limit, jams, reload actions.
// Pure logic. Magazines are ItemStore items; rounds only ever move between ammo stacks, magazine payloads, chamber and
// internal magazine, so the sum of rounds is conserved except for rounds actually fired (tests assert this).
//
// Decisions (see tests):
//  * Magazine payload lives in ItemInstance::Aux = (rounds << 8) | (ammoIndex + 1). Qty stays 1 because stores require Qty >= 1
//    and magazines have stack_max 1; an empty magazine has Aux == 0 and forgets its ammo type.
//  * Actions (cycle, clear jam, reload, load shells) are two-phase: Begin* validates and fixes the due time, Complete* performs the
//    ItemStore transaction. CancelAction() drops a pending action without touching anything (e.g. weapon swap, sprint, damage).
//  * Clearing a jam keeps the chambered round; no round is ever destroyed except by firing.
#include "GameData.h"
#include "Inventory.h"
#include "Rng.h"
#include <string>

namespace Scavar
{
enum class FireMode : uint8_t { Single = 0, Auto };
enum class WeaponAction : uint8_t { SemiAuto = 0, Bolt, BreakOpen };

struct WeaponTuning
{
    float RpmTolerance = 1.10f;         // AT-013: shots faster than RPM * 1.1 are rejected
    float WearPerShot = 0.02f;          // condition points
    float TacticalReloadSeconds = 2.2f; // magazine swap with a round already chambered
    float EmptyReloadSeconds = 3.0f;    // magazine swap + charging
    float BoltCycleSeconds = 0.9f;
    float ChargeSeconds = 0.6f;         // charging handle on semi/auto weapons
    float InternalRoundSeconds = 0.6f;  // per round/shell loaded into an internal magazine or barrel
    float BreakOpenSeconds = 0.8f;      // opening/closing a break-action gun
    float MagRoundSeconds = 0.15f;      // loading one round into a magazine (SYS-03 rule 7)
};
const WeaponTuning& DefaultWeaponTuning();

struct WeaponSpec
{
    const char* WeaponId;
    WeaponAction Action;
    bool ExternalMag;
    const char* MagDefId;   // nullptr for internal magazines
};
const WeaponSpec* FindWeaponSpec(const std::string& weaponId);
const WeaponSpec* FindSpecByMagazine(const std::string& magDefId);

constexpr int32_t MakeMagAux(int ammoIndex, int rounds) { return (ammoIndex < 0 || rounds <= 0) ? 0 : static_cast<int32_t>((rounds << 8) | (ammoIndex + 1)); }
constexpr int MagAmmoIndex(int32_t aux) { return (aux & 0xFF) - 1; }
constexpr int MagRounds(int32_t aux) { return (aux >> 8) & 0xFFFF; }
int AmmoIndexOf(const GameData& game, const std::string& ammoId);   // -1 if unknown

enum class ActionKind : uint8_t { None = 0, ClearJam, Cycle, ReloadMagazine, LoadInternal };

struct PendingAction
{
    ActionKind Kind = ActionKind::None;
    double DueAt = 0;
    ItemId Item = InvalidId;        // new magazine / ammo stack
    ContainerId Slot = InvalidId;   // where the attached magazine lives
    ContainerId Drop = InvalidId;   // where the old magazine goes
    int Count = 0;
};

struct WeaponState
{
    std::string WeaponId;
    FireMode Mode = FireMode::Single;
    float Condition = 100.f;
    int ChamberAmmo = -1;           // index into GameData::Ammo, -1 = empty
    ItemId Magazine = InvalidId;    // attached magazine item (external-magazine weapons)
    int InternalAmmo = -1;
    int InternalRounds = 0;         // internal magazine / barrels (break-action: both barrels)
    bool Jammed = false;
    bool NeedsCycle = false;        // bolt-action: spent case still in the chamber
    double LastShotAt = -1e9;
    PendingAction Pending;
};

enum class WeaponStatus : uint8_t
{
    Ok = 0, NoWeapon, Busy, Jammed, RateLimited, EmptyChamber, NeedsCycle, WrongCaliber, WrongMagazine, WrongAmmo,
    MagazineFull, InternalFull, NothingToDo, TooEarly, NoAction, NoSpace, StoreError, NotSupported, ModeUnsupported, InvalidArgument
};
const char* WeaponStatusName(WeaponStatus s);

WeaponStatus MakeWeaponState(const GameData& game, const std::string& weaponId, WeaponState* out);
WeaponStatus SetFireMode(WeaponState& w, const GameData& game, FireMode mode);

struct FireResult
{
    WeaponStatus Status = WeaponStatus::Ok;
    int AmmoIndex = -1;
    std::string AmmoId;
    int Projectiles = 0;            // pellets for shotguns
    float ProjectileDamage = 0.f;   // per projectile, before zone multipliers
    int PenTier = 0;
    float MuzzleVelocity = 0.f;
};

// Pulls the trigger once. Consumes exactly one round on Ok. Nothing changes on any other status except Jammed (the weapon jams).
FireResult Fire(WeaponState& w, ItemStore& store, const GameData& game, double now, Rng& rng, const WeaponTuning& t = DefaultWeaponTuning());

// ---- timed actions ----
WeaponStatus BeginClearJam(WeaponState& w, double now, const WeaponTuning& t = DefaultWeaponTuning());
WeaponStatus BeginCycle(WeaponState& w, const ItemStore& store, double now, const WeaponTuning& t = DefaultWeaponTuning());
WeaponStatus BeginReloadMagazine(WeaponState& w, const ItemStore& store, const GameData& game, ItemId newMagazine, ContainerId slot, ContainerId drop, double now,
                                 const WeaponTuning& t = DefaultWeaponTuning());
WeaponStatus BeginLoadInternal(WeaponState& w, const ItemStore& store, const GameData& game, ItemId ammoStack, int count, double now,
                               const WeaponTuning& t = DefaultWeaponTuning());
WeaponStatus CompleteAction(WeaponState& w, ItemStore& store, const GameData& game, double now);
void CancelAction(WeaponState& w);

// ---- inventory-side helpers (instant, atomic; callers schedule the time with MagLoadSeconds) ----
// Moves up to `count` rounds from an ammo stack into a magazine. `*loaded` receives the number moved.
WeaponStatus LoadRoundsIntoMagazine(ItemStore& store, const GameData& game, ItemId magazine, ItemId ammoStack, int count, int* loaded);
// Returns all rounds of a magazine to stacks in `dst` (merging with existing stacks first). NoSpace leaves everything unchanged.
WeaponStatus UnloadMagazine(ItemStore& store, const GameData& game, ItemId magazine, ContainerId dst);
// Returns the chambered round and the internal magazine to `dst`. NoSpace leaves everything unchanged.
WeaponStatus UnloadWeaponRounds(WeaponState& w, ItemStore& store, const GameData& game, ContainerId dst);
float MagLoadSeconds(int rounds, const WeaponTuning& t = DefaultWeaponTuning());

// Rounds currently inside the weapon: chamber + internal + attached magazine.
int RoundsInWeapon(const WeaponState& w, const ItemStore& store);
} // namespace Scavar
