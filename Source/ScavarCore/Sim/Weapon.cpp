#include "Weapon.h"
#include "Ballistics.h"
#include <algorithm>
#include <cstring>

namespace Scavar
{
namespace
{
// THE table of weapon handling. External-magazine weapons name their magazine item; SKS is intentionally internal (clip).
const WeaponSpec kSpecs[] = {
    {"W_PM",      WeaponAction::SemiAuto,  true,  "MAG_PM_8"},
    {"W_TT33",    WeaponAction::SemiAuto,  true,  "MAG_TT33_8"},
    {"W_RIM22",   WeaponAction::SemiAuto,  true,  "MAG_RIM22_10"},
    {"W_AKM",     WeaponAction::SemiAuto,  true,  "MAG_AKM_30"},
    {"W_AK74",    WeaponAction::SemiAuto,  true,  "MAG_AK74_30"},
    {"W_AKS74U",  WeaponAction::SemiAuto,  true,  "MAG_AK74_30"},
    {"W_SVD",     WeaponAction::SemiAuto,  true,  "MAG_SVD_10"},
    {"W_HUNT762", WeaponAction::SemiAuto,  true,  "MAG_HUNT762_5"},
    {"W_SKS",     WeaponAction::SemiAuto,  false, nullptr},
    {"W_MOSIN",   WeaponAction::Bolt,      false, nullptr},
    {"W_TOZ34",   WeaponAction::BreakOpen, false, nullptr},
    {"W_IZH27",   WeaponAction::BreakOpen, false, nullptr},
    {"W_OBREZ",   WeaponAction::BreakOpen, false, nullptr},
};

bool HasMode(const WeaponDef& wd, const char* mode)
{
    for (const auto& m : wd.FireModes) if (m == mode) return true;
    return false;
}

WeaponStatus FromStore(Status s) { return s == Status::NoSpace ? WeaponStatus::NoSpace : WeaponStatus::StoreError; }

// Chambers the next round from the magazine / internal magazine. Returns false when nothing was available.
bool FeedChamber(WeaponState& w, ItemStore& store, const WeaponSpec& spec)
{
    if (spec.Action == WeaponAction::BreakOpen) return false;
    if (spec.ExternalMag)
    {
        const ItemInstance* mag = w.Magazine != InvalidId ? store.GetItem(w.Magazine) : nullptr;
        if (!mag) return false;
        const int rounds = MagRounds(mag->Aux), idx = MagAmmoIndex(mag->Aux);
        if (rounds <= 0 || idx < 0) return false;
        if (store.SetAux(w.Magazine, MakeMagAux(idx, rounds - 1)) != Status::Ok) return false;
        w.ChamberAmmo = idx;
        return true;
    }
    if (w.InternalRounds <= 0) return false;
    w.ChamberAmmo = w.InternalAmmo;
    if (--w.InternalRounds == 0) w.InternalAmmo = -1;
    return true;
}

bool CanFeed(const WeaponState& w, const ItemStore& store, const WeaponSpec& spec)
{
    if (spec.Action == WeaponAction::BreakOpen) return false;
    if (spec.ExternalMag)
    {
        const ItemInstance* mag = w.Magazine != InvalidId ? store.GetItem(w.Magazine) : nullptr;
        return mag && MagRounds(mag->Aux) > 0;
    }
    return w.InternalRounds > 0;
}

// Removes `n` rounds from a stack (deletes the item when it reaches zero).
Status ConsumeRounds(ItemStore& store, ItemId stack, int n)
{
    const ItemInstance* it = store.GetItem(stack);
    if (!it) return Status::NoSuchItem;
    if (n < 1 || n > it->Qty) return Status::InvalidArgument;
    if (n == it->Qty) return store.Remove(stack);
    return store.SetQty(stack, it->Qty - n);
}

// Puts `n` rounds of `defId` into stacks in `dst`: fills existing stacks first, then creates new ones.
Status ReturnRounds(ItemStore& store, const std::string& defId, int n, ContainerId dst)
{
    const ItemCatalog* cat = store.GetCatalog();
    const ItemDef* def = cat ? cat->Find(defId) : nullptr;
    if (!def) return Status::NoSuchDef;
    if (!store.GetContainer(dst)) return Status::NoSuchContainer;
    const std::vector<ItemId> existing = store.ItemsIn(dst);
    for (ItemId id : existing)
    {
        if (n <= 0) break;
        const ItemInstance* it = store.GetItem(id);
        if (!it || it->DefId != defId || it->Aux != 0 || it->Flags != 0 || it->Qty >= def->StackMax) continue;
        const int take = std::min(n, def->StackMax - it->Qty);
        if (Status s = store.SetQty(id, it->Qty + take); s != Status::Ok) return s;
        n -= take;
    }
    while (n > 0)
    {
        const int chunk = std::min(n, def->StackMax);
        if (Status s = store.AutoPlace(defId, chunk, dst, true); s != Status::Ok) return s;
        n -= chunk;
    }
    return Status::Ok;
}
} // namespace

const WeaponTuning& DefaultWeaponTuning() { static const WeaponTuning t; return t; }

const WeaponSpec* FindWeaponSpec(const std::string& id)
{
    for (const auto& s : kSpecs) if (id == s.WeaponId) return &s;
    return nullptr;
}

const WeaponSpec* FindSpecByMagazine(const std::string& magDefId)
{
    for (const auto& s : kSpecs) if (s.MagDefId && magDefId == s.MagDefId) return &s;
    return nullptr;
}

int AmmoIndexOf(const GameData& game, const std::string& ammoId)
{
    for (size_t i = 0; i < game.Ammo.size(); ++i) if (game.Ammo[i].Id == ammoId) return static_cast<int>(i);
    return -1;
}

const char* WeaponStatusName(WeaponStatus s)
{
    static const char* const names[] = {"Ok", "NoWeapon", "Busy", "Jammed", "RateLimited", "EmptyChamber", "NeedsCycle", "WrongCaliber", "WrongMagazine",
                                        "WrongAmmo", "MagazineFull", "InternalFull", "NothingToDo", "TooEarly", "NoAction", "NoSpace", "StoreError",
                                        "NotSupported", "ModeUnsupported", "InvalidArgument"};
    return names[static_cast<size_t>(s)];
}

WeaponStatus MakeWeaponState(const GameData& game, const std::string& weaponId, WeaponState* out)
{
    if (!FindWeaponSpec(weaponId) || !game.FindWeapon(weaponId)) return WeaponStatus::NoWeapon;
    *out = WeaponState{};
    out->WeaponId = weaponId;
    return WeaponStatus::Ok;
}

WeaponStatus SetFireMode(WeaponState& w, const GameData& game, FireMode mode)
{
    const WeaponDef* wd = game.FindWeapon(w.WeaponId);
    if (!wd || !FindWeaponSpec(w.WeaponId)) return WeaponStatus::NoWeapon;
    if (w.Pending.Kind != ActionKind::None) return WeaponStatus::Busy;
    if (mode == FireMode::Auto && !HasMode(*wd, "auto")) return WeaponStatus::ModeUnsupported;
    w.Mode = mode;
    return WeaponStatus::Ok;
}

FireResult Fire(WeaponState& w, ItemStore& store, const GameData& game, double now, Rng& rng, const WeaponTuning& t)
{
    FireResult r;
    const WeaponSpec* spec = FindWeaponSpec(w.WeaponId);
    const WeaponDef* wd = game.FindWeapon(w.WeaponId);
    if (!spec || !wd) { r.Status = WeaponStatus::NoWeapon; return r; }
    if (w.Pending.Kind != ActionKind::None) { r.Status = WeaponStatus::Busy; return r; }
    if (w.Jammed) { r.Status = WeaponStatus::Jammed; return r; }
    if (w.Mode == FireMode::Auto && !HasMode(*wd, "auto")) { r.Status = WeaponStatus::ModeUnsupported; return r; }

    int idx = -1;
    if (spec->Action == WeaponAction::BreakOpen)
    {
        if (w.InternalRounds <= 0) { r.Status = WeaponStatus::EmptyChamber; return r; }
        idx = w.InternalAmmo;
    }
    else
    {
        if (w.NeedsCycle) { r.Status = WeaponStatus::NeedsCycle; return r; }
        idx = w.ChamberAmmo;
    }
    if (idx < 0) { r.Status = WeaponStatus::EmptyChamber; return r; }
    if (idx >= static_cast<int>(game.Ammo.size())) { r.Status = WeaponStatus::WrongAmmo; return r; }
    const AmmoDef& ammo = game.Ammo[static_cast<size_t>(idx)];
    if (ammo.Caliber != wd->Caliber) { r.Status = WeaponStatus::WrongCaliber; return r; }

    if (wd->Rpm > 0.f)
    {
        const double minInterval = 60.0 / (static_cast<double>(wd->Rpm) * t.RpmTolerance);
        if (now - w.LastShotAt < minInterval - 1e-9) { r.Status = WeaponStatus::RateLimited; return r; }
    }
    if (RollJam(w.Condition, rng)) { w.Jammed = true; r.Status = WeaponStatus::Jammed; return r; }

    if (spec->Action == WeaponAction::BreakOpen)
    {
        if (--w.InternalRounds == 0) w.InternalAmmo = -1;
    }
    else
    {
        w.ChamberAmmo = -1;
        if (spec->Action == WeaponAction::Bolt) w.NeedsCycle = true;
        else FeedChamber(w, store, *spec);
    }
    w.LastShotAt = now;
    w.Condition = std::max(0.f, w.Condition - t.WearPerShot);

    r.Status = WeaponStatus::Ok;
    r.AmmoIndex = idx;
    r.AmmoId = ammo.Id;
    r.Projectiles = ammo.Pellets;
    r.ProjectileDamage = ammo.Damage;
    r.PenTier = ammo.PenTier;
    r.MuzzleVelocity = ammo.MuzzleVelocity;
    return r;
}

WeaponStatus BeginClearJam(WeaponState& w, double now, const WeaponTuning&)
{
    if (!FindWeaponSpec(w.WeaponId)) return WeaponStatus::NoWeapon;
    if (w.Pending.Kind != ActionKind::None) return WeaponStatus::Busy;
    if (!w.Jammed) return WeaponStatus::NothingToDo;
    w.Pending = PendingAction{ActionKind::ClearJam, now + static_cast<double>(DefaultBallisticsTuning().JamClearSeconds)};
    return WeaponStatus::Ok;
}

WeaponStatus BeginCycle(WeaponState& w, const ItemStore& store, double now, const WeaponTuning& t)
{
    const WeaponSpec* spec = FindWeaponSpec(w.WeaponId);
    if (!spec) return WeaponStatus::NoWeapon;
    if (w.Pending.Kind != ActionKind::None) return WeaponStatus::Busy;
    if (spec->Action == WeaponAction::BreakOpen) return WeaponStatus::NotSupported;
    if (w.Jammed) return WeaponStatus::Jammed;
    const bool bolt = spec->Action == WeaponAction::Bolt;
    const bool wanted = bolt ? (w.NeedsCycle || w.ChamberAmmo < 0) : (w.ChamberAmmo < 0);
    if (!wanted) return WeaponStatus::NothingToDo;
    if (!w.NeedsCycle && !CanFeed(w, store, *spec)) return WeaponStatus::NothingToDo;
    w.Pending = PendingAction{ActionKind::Cycle, now + static_cast<double>(bolt ? t.BoltCycleSeconds : t.ChargeSeconds)};
    return WeaponStatus::Ok;
}

WeaponStatus BeginReloadMagazine(WeaponState& w, const ItemStore& store, const GameData& game, ItemId newMagazine, ContainerId slot, ContainerId drop, double now,
                                 const WeaponTuning& t)
{
    const WeaponSpec* spec = FindWeaponSpec(w.WeaponId);
    const WeaponDef* wd = game.FindWeapon(w.WeaponId);
    if (!spec || !wd) return WeaponStatus::NoWeapon;
    if (w.Pending.Kind != ActionKind::None) return WeaponStatus::Busy;
    if (!spec->ExternalMag) return WeaponStatus::NotSupported;
    const ItemInstance* mag = store.GetItem(newMagazine);
    if (!mag || newMagazine == w.Magazine) return WeaponStatus::InvalidArgument;
    if (mag->DefId != spec->MagDefId) return WeaponStatus::WrongMagazine;
    if (MagRounds(mag->Aux) > 0)
    {
        const int idx = MagAmmoIndex(mag->Aux);
        if (idx < 0 || idx >= static_cast<int>(game.Ammo.size()) || game.Ammo[static_cast<size_t>(idx)].Caliber != wd->Caliber) return WeaponStatus::WrongAmmo;
    }
    if (!store.GetContainer(slot) || !store.GetContainer(drop)) return WeaponStatus::InvalidArgument;
    const bool tactical = w.ChamberAmmo >= 0;
    w.Pending = PendingAction{ActionKind::ReloadMagazine, now + static_cast<double>(tactical ? t.TacticalReloadSeconds : t.EmptyReloadSeconds), newMagazine, slot, drop, 0};
    return WeaponStatus::Ok;
}

WeaponStatus BeginLoadInternal(WeaponState& w, const ItemStore& store, const GameData& game, ItemId ammoStack, int count, double now, const WeaponTuning& t)
{
    const WeaponSpec* spec = FindWeaponSpec(w.WeaponId);
    const WeaponDef* wd = game.FindWeapon(w.WeaponId);
    if (!spec || !wd) return WeaponStatus::NoWeapon;
    if (w.Pending.Kind != ActionKind::None) return WeaponStatus::Busy;
    if (spec->ExternalMag) return WeaponStatus::NotSupported;
    const ItemInstance* stack = store.GetItem(ammoStack);
    if (!stack || count < 1) return WeaponStatus::InvalidArgument;
    const int idx = AmmoIndexOf(game, stack->DefId);
    if (idx < 0) return WeaponStatus::WrongAmmo;
    if (game.Ammo[static_cast<size_t>(idx)].Caliber != wd->Caliber) return WeaponStatus::WrongCaliber;
    const int space = wd->MagSize - w.InternalRounds;
    if (space <= 0) return WeaponStatus::InternalFull;
    if (w.InternalRounds > 0 && w.InternalAmmo != idx) return WeaponStatus::WrongAmmo;
    const int n = std::min({count, space, stack->Qty});
    float seconds = static_cast<float>(n) * t.InternalRoundSeconds;
    if (spec->Action == WeaponAction::BreakOpen) seconds += t.BreakOpenSeconds;
    w.Pending = PendingAction{ActionKind::LoadInternal, now + static_cast<double>(seconds), ammoStack, InvalidId, InvalidId, n};
    return WeaponStatus::Ok;
}

void CancelAction(WeaponState& w) { w.Pending = PendingAction{}; }

WeaponStatus CompleteAction(WeaponState& w, ItemStore& store, const GameData& game, double now)
{
    const PendingAction act = w.Pending;
    if (act.Kind == ActionKind::None) return WeaponStatus::NoAction;
    if (now + 1e-5 < act.DueAt) return WeaponStatus::TooEarly;   // float tunables (2.2f) are not exact in double
    const WeaponSpec* spec = FindWeaponSpec(w.WeaponId);
    const WeaponDef* wd = game.FindWeapon(w.WeaponId);
    w.Pending = PendingAction{};
    if (!spec || !wd) return WeaponStatus::NoWeapon;

    switch (act.Kind)
    {
    case ActionKind::ClearJam:
        w.Jammed = false;
        return WeaponStatus::Ok;

    case ActionKind::Cycle:
    {
        store.BeginTx();
        WeaponState next = w;
        next.NeedsCycle = false;
        if (next.ChamberAmmo < 0) FeedChamber(next, store, *spec);
        if (store.Commit() != Status::Ok) return WeaponStatus::StoreError;
        w = next;
        return WeaponStatus::Ok;
    }

    case ActionKind::ReloadMagazine:
    {
        const ItemInstance* nm = store.GetItem(act.Item);
        if (!nm) return WeaponStatus::InvalidArgument;
        const ContainerId from = nm->Container;
        store.BeginTx();
        WeaponState next = w;
        Status s = Status::Ok;
        if (w.Magazine != InvalidId && store.GetItem(w.Magazine)) s = store.AutoMove(w.Magazine, act.Drop, true, act.Slot);
        if (s == Status::Ok) s = store.Move(act.Item, act.Slot, 0, 0, false, from);
        if (s != Status::Ok) { store.Rollback(); return FromStore(s); }
        next.Magazine = act.Item;
        if (next.ChamberAmmo < 0 && !next.NeedsCycle) FeedChamber(next, store, *spec);   // an empty reload includes charging
        if (store.Commit() != Status::Ok) return WeaponStatus::StoreError;
        w = next;
        return WeaponStatus::Ok;
    }

    case ActionKind::LoadInternal:
    {
        const ItemInstance* stack = store.GetItem(act.Item);
        if (!stack) return WeaponStatus::InvalidArgument;
        const int idx = AmmoIndexOf(game, stack->DefId);
        if (idx < 0 || act.Count > stack->Qty) return WeaponStatus::InvalidArgument;
        if ((w.InternalRounds > 0 && w.InternalAmmo != idx) || w.InternalRounds + act.Count > wd->MagSize) return WeaponStatus::InternalFull;
        store.BeginTx();
        if (Status s = ConsumeRounds(store, act.Item, act.Count); s != Status::Ok) { store.Rollback(); return FromStore(s); }
        if (store.Commit() != Status::Ok) return WeaponStatus::StoreError;
        w.InternalAmmo = idx;
        w.InternalRounds += act.Count;
        return WeaponStatus::Ok;
    }
    case ActionKind::None:
        break;
    }
    return WeaponStatus::NoAction;
}

WeaponStatus LoadRoundsIntoMagazine(ItemStore& store, const GameData& game, ItemId magazine, ItemId ammoStack, int count, int* loaded)
{
    if (loaded) *loaded = 0;
    const ItemInstance* mag = store.GetItem(magazine);
    const ItemInstance* stack = store.GetItem(ammoStack);
    if (!mag || !stack || count < 1) return WeaponStatus::InvalidArgument;
    const WeaponSpec* spec = FindSpecByMagazine(mag->DefId);
    if (!spec) return WeaponStatus::WrongMagazine;
    const WeaponDef* wd = game.FindWeapon(spec->WeaponId);
    if (!wd) return WeaponStatus::NoWeapon;
    const int idx = AmmoIndexOf(game, stack->DefId);
    if (idx < 0) return WeaponStatus::WrongAmmo;
    if (game.Ammo[static_cast<size_t>(idx)].Caliber != wd->Caliber) return WeaponStatus::WrongCaliber;
    const int rounds = MagRounds(mag->Aux);
    if (rounds > 0 && MagAmmoIndex(mag->Aux) != idx) return WeaponStatus::WrongAmmo;
    const int space = wd->MagSize - rounds;
    if (space <= 0) return WeaponStatus::MagazineFull;
    const int n = std::min({count, space, stack->Qty});

    store.BeginTx();
    Status s = ConsumeRounds(store, ammoStack, n);
    if (s == Status::Ok) s = store.SetAux(magazine, MakeMagAux(idx, rounds + n));
    if (s != Status::Ok) { store.Rollback(); return FromStore(s); }
    if (store.Commit() != Status::Ok) return WeaponStatus::StoreError;
    if (loaded) *loaded = n;
    return WeaponStatus::Ok;
}

WeaponStatus UnloadMagazine(ItemStore& store, const GameData& game, ItemId magazine, ContainerId dst)
{
    const ItemInstance* mag = store.GetItem(magazine);
    if (!mag) return WeaponStatus::InvalidArgument;
    const int rounds = MagRounds(mag->Aux), idx = MagAmmoIndex(mag->Aux);
    if (rounds <= 0 || idx < 0 || idx >= static_cast<int>(game.Ammo.size())) return WeaponStatus::NothingToDo;
    store.BeginTx();
    Status s = ReturnRounds(store, game.Ammo[static_cast<size_t>(idx)].Id, rounds, dst);
    if (s == Status::Ok) s = store.SetAux(magazine, 0);
    if (s != Status::Ok) { store.Rollback(); return FromStore(s); }
    if (store.Commit() != Status::Ok) return WeaponStatus::StoreError;
    return WeaponStatus::Ok;
}

WeaponStatus UnloadWeaponRounds(WeaponState& w, ItemStore& store, const GameData& game, ContainerId dst)
{
    if (!FindWeaponSpec(w.WeaponId)) return WeaponStatus::NoWeapon;
    if (w.Pending.Kind != ActionKind::None) return WeaponStatus::Busy;
    if (w.ChamberAmmo < 0 && w.InternalRounds <= 0) return WeaponStatus::NothingToDo;
    store.BeginTx();
    Status s = Status::Ok;
    if (w.ChamberAmmo >= 0) s = ReturnRounds(store, game.Ammo[static_cast<size_t>(w.ChamberAmmo)].Id, 1, dst);
    if (s == Status::Ok && w.InternalRounds > 0) s = ReturnRounds(store, game.Ammo[static_cast<size_t>(w.InternalAmmo)].Id, w.InternalRounds, dst);
    if (s != Status::Ok) { store.Rollback(); return FromStore(s); }
    if (store.Commit() != Status::Ok) return WeaponStatus::StoreError;
    w.ChamberAmmo = -1;
    w.InternalAmmo = -1;
    w.InternalRounds = 0;
    w.NeedsCycle = false;
    return WeaponStatus::Ok;
}

float MagLoadSeconds(int rounds, const WeaponTuning& t) { return static_cast<float>(rounds < 0 ? 0 : rounds) * t.MagRoundSeconds; }

int RoundsInWeapon(const WeaponState& w, const ItemStore& store)
{
    int n = (w.ChamberAmmo >= 0 ? 1 : 0) + w.InternalRounds;
    if (w.Magazine != InvalidId) if (const ItemInstance* mag = store.GetItem(w.Magazine)) n += MagRounds(mag->Aux);
    return n;
}
} // namespace Scavar
