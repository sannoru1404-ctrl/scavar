#include "TestFramework.h"
#include "Items.h"
#include "Weapon.h"
#include <memory>

using namespace Scavar;

namespace
{
// Items from items.csv plus every ammo type and weapon magazine that the slice table does not list yet.
struct Env
{
    GameData game;
    ItemTable table;
    ItemCatalog cat;
    ItemStore store{&cat};
    ContainerId bag = 0, slot = 0, slot2 = 0;
    bool ok = false;

    explicit Env(int bagW = 10, int bagH = 10)
    {
        std::string err;
        ok = game.Load(SCAVAR_DATA_DIR, &err) && table.Load(SCAVAR_DATA_DIR, game, &err) && table.BuildCatalog(cat);
        if (!ok) std::printf("    env error: %s\n", err.c_str());
        for (const auto& a : game.Ammo) if (!cat.Find(a.Id)) cat.Register(ItemDef{a.Id, 1, 1, 0.02f, 60});
        for (const char* mag : {"MAG_TT33_8", "MAG_AKM_30", "MAG_AK74_30", "MAG_SVD_10", "MAG_HUNT762_5"}) if (!cat.Find(mag)) cat.Register(ItemDef{mag, 1, 1, 0.1f, 1});
        Container b; b.Kind = ContainerKind::Backpack; b.W = bagW; b.H = bagH;
        store.AddContainer(b, &bag);
        Container s; s.Kind = ContainerKind::Pockets; s.W = 1; s.H = 1;
        store.AddContainer(s, &slot);
        store.AddContainer(s, &slot2);
    }
    ItemId Make(const std::string& def, int qty)
    {
        ItemId id = 0;
        CHECK(store.AutoPlace(def, qty, bag, true, &id) == Status::Ok);
        return id;
    }
    ItemId Mag(const std::string& def, int ammoIndex, int rounds)
    {
        ItemId id = Make(def, 1);
        CHECK(store.SetAux(id, MakeMagAux(ammoIndex, rounds)) == Status::Ok);
        return id;
    }
    int Ammo(const char* id) const { return AmmoIndexOf(game, id); }
};

// Every round that exists: stacks + magazine payloads + chambers + internal magazines.
long TotalRounds(const Env& e, const WeaponState& w)
{
    long n = (w.ChamberAmmo >= 0 ? 1 : 0) + w.InternalRounds;
    for (const auto& [id, it] : e.store.AllItems())
    {
        (void)id;
        if (e.game.FindAmmo(it.DefId)) n += it.Qty;
        else if (FindSpecByMagazine(it.DefId)) n += MagRounds(it.Aux);
    }
    return n;
}

bool Advance(Env& e, WeaponState& w, double now)
{
    return CompleteAction(w, e.store, e.game, now) == WeaponStatus::Ok;
}
}

TEST(MagazineAux_PacksAmmoTypeAndRounds)
{
    CHECK_EQ(MakeMagAux(3, 0), 0);
    CHECK_EQ(MakeMagAux(-1, 5), 0);
    const int32_t aux = MakeMagAux(11, 30);
    CHECK_EQ(MagAmmoIndex(aux), 11);
    CHECK_EQ(MagRounds(aux), 30);
    CHECK_EQ(MagAmmoIndex(0), -1);
}

TEST(AT012_ServerRejectsShotsWithoutAmmoOrWithWrongAmmo)
{
    Env e; CHECK(e.ok);
    Rng rng(1);
    WeaponState pm;
    CHECK(MakeWeaponState(e.game, "W_PM", &pm) == WeaponStatus::Ok);
    CHECK(Fire(pm, e.store, e.game, 0.0, rng).Status == WeaponStatus::EmptyChamber);   // nothing loaded

    pm.ChamberAmmo = e.Ammo("A_545_PS");                                               // tampered state: wrong caliber
    CHECK(Fire(pm, e.store, e.game, 1.0, rng).Status == WeaponStatus::WrongCaliber);
    pm.ChamberAmmo = 9999;
    CHECK(Fire(pm, e.store, e.game, 2.0, rng).Status == WeaponStatus::WrongAmmo);
    pm.ChamberAmmo = -1;

    // Loading is just as strict: wrong caliber, mixed types, wrong magazine for the gun.
    ItemId mag = e.Make("MAG_PM_8", 1);
    ItemId rifleAmmo = e.Make("A_545_PS", 30);
    ItemId pmAmmo = e.Make("A_918_FMJ", 20);
    ItemId apAmmo = e.Make("A_918_AP", 10);
    int loaded = -1;
    CHECK(LoadRoundsIntoMagazine(e.store, e.game, mag, rifleAmmo, 10, &loaded) == WeaponStatus::WrongCaliber);
    CHECK_EQ(loaded, 0);
    CHECK(LoadRoundsIntoMagazine(e.store, e.game, mag, pmAmmo, 3, &loaded) == WeaponStatus::Ok);
    CHECK(LoadRoundsIntoMagazine(e.store, e.game, mag, apAmmo, 3, &loaded) == WeaponStatus::WrongAmmo);
    CHECK_EQ(e.store.GetItem(apAmmo)->Qty, 10);
    CHECK(LoadRoundsIntoMagazine(e.store, e.game, rifleAmmo, pmAmmo, 3, &loaded) == WeaponStatus::WrongMagazine);   // not a magazine

    ItemId rimMag = e.Make("MAG_RIM22_10", 1);
    CHECK(BeginReloadMagazine(pm, e.store, e.game, rimMag, e.slot, e.bag, 0.0) == WeaponStatus::WrongMagazine);

    WeaponState knife;
    CHECK(MakeWeaponState(e.game, "M_KNIFE", &knife) == WeaponStatus::NoWeapon);
    CHECK(SetFireMode(pm, e.game, FireMode::Auto) == WeaponStatus::ModeUnsupported);
    CHECK(Fire(pm, e.store, e.game, 3.0, rng).Status == WeaponStatus::EmptyChamber);
    CHECK_EQ(pm.LastShotAt, -1e9);   // rejected shots leave no trace
}

TEST(AT013_RpmLimitRejectsFasterThanTenPercentOverRpm)
{
    Env e; CHECK(e.ok);
    Rng rng(2);
    WeaponState pm;
    MakeWeaponState(e.game, "W_PM", &pm);
    const int pmIdx = e.Ammo("A_918_FMJ");
    ItemId mag = e.Mag("MAG_PM_8", pmIdx, 8);
    CHECK(BeginReloadMagazine(pm, e.store, e.game, mag, e.slot, e.bag, 0.0) == WeaponStatus::Ok);
    CHECK(Advance(e, pm, 3.0));
    const long before = TotalRounds(e, pm);

    CHECK(Fire(pm, e.store, e.game, 10.00, rng).Status == WeaponStatus::Ok);        // 360 rpm -> min interval 60/396 = 0.1515 s
    CHECK(Fire(pm, e.store, e.game, 10.10, rng).Status == WeaponStatus::RateLimited);
    CHECK_EQ(TotalRounds(e, pm), before - 1);                                      // a rejected shot consumes nothing
    CHECK(Fire(pm, e.store, e.game, 10.16, rng).Status == WeaponStatus::Ok);

    // Full-auto rifle held down for 2 s, polled every 10 ms: never more than RPM * 1.1.
    WeaponState akm;
    MakeWeaponState(e.game, "W_AKM", &akm);
    CHECK(SetFireMode(akm, e.game, FireMode::Auto) == WeaponStatus::Ok);
    ItemId akmMag = e.Mag("MAG_AKM_30", e.Ammo("A_76239_PS"), 30);
    CHECK(BeginReloadMagazine(akm, e.store, e.game, akmMag, e.slot2, e.bag, 0.0) == WeaponStatus::Ok);
    CHECK(Advance(e, akm, 3.0));
    int shots = 0;
    for (int i = 0; i <= 200; ++i)
        if (Fire(akm, e.store, e.game, 20.0 + i * 0.01, rng).Status == WeaponStatus::Ok) ++shots;
    CHECK(shots <= 23);                // 2 s * 660/60 = 22 intervals + the first shot
    CHECK(shots >= 20);
    CHECK_EQ(RoundsInWeapon(akm, e.store), 30 - shots);
}

TEST(Weapon_PistolReloadFlowTacticalAndEmpty)
{
    Env e; CHECK(e.ok);
    Rng rng(3);
    const int idx = e.Ammo("A_918_FMJ");
    ItemId stack = e.Make("A_918_FMJ", 30);
    ItemId m1 = e.Make("MAG_PM_8", 1), m2 = e.Make("MAG_PM_8", 1);
    int loaded = 0;
    CHECK(LoadRoundsIntoMagazine(e.store, e.game, m1, stack, 20, &loaded) == WeaponStatus::Ok);
    CHECK_EQ(loaded, 8);                                   // capped by magazine size
    CHECK(LoadRoundsIntoMagazine(e.store, e.game, m2, stack, 20, &loaded) == WeaponStatus::Ok);
    CHECK_EQ(loaded, 8);
    CHECK_EQ(e.store.GetItem(stack)->Qty, 14);
    CHECK(LoadRoundsIntoMagazine(e.store, e.game, m1, stack, 1, &loaded) == WeaponStatus::MagazineFull);
    CHECK_NEAR(MagLoadSeconds(8), 1.2, 1e-6);
    const long total = 30;

    WeaponState w;
    MakeWeaponState(e.game, "W_PM", &w);
    CHECK(BeginReloadMagazine(w, e.store, e.game, m1, e.slot, e.bag, 100.0) == WeaponStatus::Ok);   // empty reload: charges the gun
    CHECK(CompleteAction(w, e.store, e.game, 102.9) == WeaponStatus::TooEarly);
    CHECK(Fire(w, e.store, e.game, 102.9, rng).Status == WeaponStatus::Busy);
    CHECK(CompleteAction(w, e.store, e.game, 103.0) == WeaponStatus::Ok);
    CHECK_EQ(w.Magazine, m1);
    CHECK_EQ(e.store.GetItem(m1)->Container, e.slot);
    CHECK_EQ(w.ChamberAmmo, idx);
    CHECK_EQ(MagRounds(e.store.GetItem(m1)->Aux), 7);
    CHECK_EQ(RoundsInWeapon(w, e.store), 8);

    int fired = 0;
    double t = 110.0;
    for (int i = 0; i < 3; ++i, t += 0.5) fired += Fire(w, e.store, e.game, t, rng).Status == WeaponStatus::Ok;
    CHECK_EQ(fired, 3);

    // Tactical reload: a round stays chambered, so it is faster, and the partly used magazine returns to the bag.
    CHECK(BeginReloadMagazine(w, e.store, e.game, m2, e.slot, e.bag, 120.0) == WeaponStatus::Ok);
    CHECK_NEAR(w.Pending.DueAt, 122.2, 1e-6);
    CHECK(CompleteAction(w, e.store, e.game, 122.2) == WeaponStatus::Ok);
    CHECK_EQ(w.Magazine, m2);
    CHECK_EQ(e.store.GetItem(m1)->Container, e.bag);
    CHECK_EQ(MagRounds(e.store.GetItem(m1)->Aux), 4);
    CHECK_EQ(MagRounds(e.store.GetItem(m2)->Aux), 8);   // chamber kept, new magazine untouched
    CHECK(w.ChamberAmmo >= 0);

    for (int i = 0; i < 20 && Fire(w, e.store, e.game, t, rng).Status == WeaponStatus::Ok; ++i, t += 0.5) ++fired;
    CHECK_EQ(fired, 3 + 9);
    CHECK(Fire(w, e.store, e.game, t + 1, rng).Status == WeaponStatus::EmptyChamber);
    CHECK_EQ(TotalRounds(e, w) + fired, total);
    std::string why;
    CHECK(e.store.CheckInvariants(&why));
}

TEST(Weapon_ReloadAndUnloadAreAtomicWhenThereIsNoSpace)
{
    Env e(2, 1);   // a 2-cell bag
    CHECK(e.ok);
    Rng rng(4);
    const int idx = e.Ammo("A_918_FMJ");
    ItemId oldMag = e.Mag("MAG_PM_8", idx, 5);
    WeaponState w;
    MakeWeaponState(e.game, "W_PM", &w);
    CHECK(BeginReloadMagazine(w, e.store, e.game, oldMag, e.slot, e.bag, 0.0) == WeaponStatus::Ok);
    CHECK(Advance(e, w, 3.0));
    ItemId newMag = e.Mag("MAG_PM_8", idx, 8);
    ItemId filler = e.Make("MAG_PM_8", 1);    // bag is now full: newMag + filler
    (void)filler;

    ItemStore snapshot = e.store;
    const WeaponState wBefore = w;
    CHECK(BeginReloadMagazine(w, e.store, e.game, newMag, e.slot, e.bag, 10.0) == WeaponStatus::Ok);
    CHECK(CompleteAction(w, e.store, e.game, 20.0) == WeaponStatus::NoSpace);   // the old magazine has nowhere to go
    CHECK(e.store.Equals(snapshot));                                          // the failed swap left no trace
    CHECK_EQ(w.Magazine, wBefore.Magazine);
    CHECK(w.ChamberAmmo == wBefore.ChamberAmmo);
    CHECK(w.Pending.Kind == ActionKind::None);
    std::string why;
    CHECK(e.store.CheckInvariants(&why));
}

TEST(Weapon_UnloadMagazineNoSpaceLeavesEverythingUnchanged)
{
    Env e(1, 1);
    CHECK(e.ok);
    Container side; side.Kind = ContainerKind::Backpack; side.W = 3; side.H = 1;
    ContainerId sideId = 0;
    e.store.AddContainer(side, &sideId);
    ItemId mag = 0;
    CHECK(e.store.Create("MAG_PM_8", 1, sideId, 0, 0, false, &mag) == Status::Ok);
    CHECK(e.store.SetAux(mag, MakeMagAux(e.Ammo("A_918_AP"), 6)) == Status::Ok);
    ItemId block = 0;
    CHECK(e.store.Create("A_918_FMJ", 5, e.bag, 0, 0, false, &block) == Status::Ok);   // bag (1 cell) is full of FMJ

    ItemStore snapshot = e.store;
    CHECK(UnloadMagazine(e.store, e.game, mag, e.bag) == WeaponStatus::NoSpace);       // AP rounds cannot merge with FMJ and there is no cell
    CHECK(e.store.Equals(snapshot));
    CHECK_EQ(MagRounds(e.store.GetItem(mag)->Aux), 6);

    CHECK(UnloadMagazine(e.store, e.game, mag, sideId) == WeaponStatus::Ok);
    CHECK_EQ(e.store.GetItem(mag)->Aux, 0);
    long ap = 0;
    for (const auto& [id, it] : e.store.AllItems()) { (void)id; if (it.DefId == "A_918_AP") ap += it.Qty; }
    CHECK_EQ(ap, 6);
}

TEST(Weapon_JamsBelowThirtyConditionAndClearsAfterTwoAndAHalfSeconds)
{
    Env e; CHECK(e.ok);
    Rng rng(5);
    WeaponState w;
    MakeWeaponState(e.game, "W_PM", &w);
    const int idx = e.Ammo("A_918_FMJ");

    // Healthy weapon never jams.
    w.Condition = 60.f;
    int jams = 0;
    for (int i = 0; i < 3000; ++i)
    {
        w.ChamberAmmo = idx; w.LastShotAt = -1e9;
        jams += Fire(w, e.store, e.game, 1.0, rng).Status == WeaponStatus::Jammed;
        w.Condition = 60.f;
    }
    CHECK_EQ(jams, 0);

    // Worn weapon: about 15% at condition 0.
    jams = 0;
    int shots = 0;
    for (int i = 0; i < 20000; ++i)
    {
        w.Jammed = false; w.ChamberAmmo = idx; w.LastShotAt = -1e9; w.Condition = 0.f;
        const auto r = Fire(w, e.store, e.game, 1.0, rng);
        if (r.Status == WeaponStatus::Jammed) ++jams; else ++shots;
    }
    CHECK_NEAR(jams / 20000.0, 0.15, 0.015);

    // A jam consumes nothing, blocks fire, and needs 2.5 s to clear.
    w = WeaponState{}; MakeWeaponState(e.game, "W_PM", &w);
    w.Condition = 0.f;
    double t = 100.0;
    while (Fire((w.ChamberAmmo = idx, w), e.store, e.game, t, rng).Status != WeaponStatus::Jammed) t += 1.0;
    CHECK(w.Jammed);
    CHECK_EQ(w.ChamberAmmo, idx);                                                    // round still chambered
    CHECK(Fire(w, e.store, e.game, t + 1, rng).Status == WeaponStatus::Jammed);
    CHECK(BeginCycle(w, e.store, t, DefaultWeaponTuning()) == WeaponStatus::Jammed);
    CHECK(BeginClearJam(w, t) == WeaponStatus::Ok);
    CHECK(BeginClearJam(w, t) == WeaponStatus::Busy);
    CHECK(CompleteAction(w, e.store, e.game, t + 2.4) == WeaponStatus::TooEarly);
    CHECK(CompleteAction(w, e.store, e.game, t + 2.5) == WeaponStatus::Ok);
    CHECK(!w.Jammed);
    CHECK(BeginClearJam(w, t + 3) == WeaponStatus::NothingToDo);
    (void)shots;
}

TEST(Weapon_BoltActionNeedsCyclingAndInternalLoading)
{
    Env e; CHECK(e.ok);
    Rng rng(6);
    WeaponState w;
    MakeWeaponState(e.game, "W_MOSIN", &w);
    ItemId stack = e.Make("A_762R_FMJ", 20);
    ItemId shells = e.Make("A_12_BUCK", 10);

    CHECK(BeginLoadInternal(w, e.store, e.game, shells, 5, 0.0) == WeaponStatus::WrongCaliber);
    CHECK(BeginLoadInternal(w, e.store, e.game, stack, 9, 0.0) == WeaponStatus::Ok);
    CHECK_NEAR(w.Pending.DueAt, 3.0, 1e-6);                  // 5 rounds fit, 0.6 s each
    CHECK(Fire(w, e.store, e.game, 1.0, rng).Status == WeaponStatus::Busy);
    CHECK(CompleteAction(w, e.store, e.game, 2.9) == WeaponStatus::TooEarly);
    CHECK(CompleteAction(w, e.store, e.game, 3.0) == WeaponStatus::Ok);
    CHECK_EQ(w.InternalRounds, 5);
    CHECK_EQ(e.store.GetItem(stack)->Qty, 15);
    CHECK(BeginLoadInternal(w, e.store, e.game, stack, 1, 3.0) == WeaponStatus::InternalFull);

    CHECK(Fire(w, e.store, e.game, 4.0, rng).Status == WeaponStatus::EmptyChamber);   // loaded but not chambered
    CHECK(BeginCycle(w, e.store, 4.0) == WeaponStatus::Ok);
    CHECK(Advance(e, w, 4.9));
    CHECK_EQ(w.InternalRounds, 4);
    int fired = 0;
    double t = 10.0;
    for (int i = 0; i < 5; ++i)
    {
        const auto r = Fire(w, e.store, e.game, t, rng);
        CHECK(r.Status == WeaponStatus::Ok);
        CHECK_EQ(r.Projectiles, 1);
        CHECK_NEAR(r.ProjectileDamage, 75, 1e-6);
        CHECK_EQ(r.PenTier, 4);
        ++fired;
        CHECK(Fire(w, e.store, e.game, t + 5, rng).Status == WeaponStatus::NeedsCycle);   // spent case must be worked out first
        CHECK(BeginCycle(w, e.store, t + 5) == WeaponStatus::Ok);
        CHECK(Advance(e, w, t + 6));
        t += 10;
    }
    CHECK_EQ(fired, 5);
    CHECK(Fire(w, e.store, e.game, t, rng).Status == WeaponStatus::EmptyChamber);         // nothing left to chamber
    CHECK(BeginCycle(w, e.store, t) == WeaponStatus::NothingToDo);
    CHECK_EQ(e.store.GetItem(stack)->Qty, 15);
}

TEST(Weapon_BreakOpenShotgunHasTwoBarrelsAndNoCycling)
{
    Env e; CHECK(e.ok);
    Rng rng(7);
    WeaponState w;
    MakeWeaponState(e.game, "W_TOZ34", &w);
    ItemId buck = e.Make("A_12_BUCK", 10);
    ItemId slug = e.Make("A_12_SLUG", 5);

    CHECK(BeginCycle(w, e.store, 0.0) == WeaponStatus::NotSupported);
    CHECK(BeginLoadInternal(w, e.store, e.game, buck, 5, 0.0) == WeaponStatus::Ok);
    CHECK_NEAR(w.Pending.DueAt, 2.0, 1e-6);                  // 2 shells * 0.6 s + 0.8 s to open and close
    CHECK(Advance(e, w, 2.0));
    CHECK_EQ(w.InternalRounds, 2);
    CHECK_EQ(e.store.GetItem(buck)->Qty, 8);
    CHECK(BeginLoadInternal(w, e.store, e.game, slug, 1, 2.0) == WeaponStatus::InternalFull);

    const auto first = Fire(w, e.store, e.game, 5.0, rng);
    CHECK(first.Status == WeaponStatus::Ok);
    CHECK_EQ(first.Projectiles, 8);
    CHECK_NEAR(first.ProjectileDamage, 14, 1e-6);
    CHECK(Fire(w, e.store, e.game, 5.2, rng).Status == WeaponStatus::RateLimited);        // 120 rpm -> 0.45 s
    CHECK(Fire(w, e.store, e.game, 5.5, rng).Status == WeaponStatus::Ok);
    CHECK(Fire(w, e.store, e.game, 9.0, rng).Status == WeaponStatus::EmptyChamber);

    // Shell types cannot be mixed in one load, but an empty gun accepts any type.
    CHECK(BeginLoadInternal(w, e.store, e.game, slug, 1, 10.0) == WeaponStatus::Ok);
    CHECK(Advance(e, w, 11.4));
    CHECK(BeginLoadInternal(w, e.store, e.game, buck, 1, 12.0) == WeaponStatus::WrongAmmo);
    CHECK(UnloadWeaponRounds(w, e.store, e.game, e.bag) == WeaponStatus::Ok);
    CHECK_EQ(w.InternalRounds, 0);
    CHECK_EQ(e.store.GetItem(slug)->Qty, 5);
}

TEST(Weapon_SemiAutoWithInternalClipFeedsItself)
{
    Env e; CHECK(e.ok);
    Rng rng(8);
    WeaponState w;
    MakeWeaponState(e.game, "W_SKS", &w);
    ItemId clip = e.Make("A_76239_PS", 40);
    CHECK(BeginLoadInternal(w, e.store, e.game, clip, 10, 0.0) == WeaponStatus::Ok);
    CHECK(Advance(e, w, 6.0));
    CHECK(BeginCycle(w, e.store, 6.0) == WeaponStatus::Ok);
    CHECK(Advance(e, w, 6.6));
    int fired = 0;
    for (int i = 0; i < 20; ++i)
        fired += Fire(w, e.store, e.game, 10.0 + i * 0.5, rng).Status == WeaponStatus::Ok;
    CHECK_EQ(fired, 10);                                      // chamber + 9 from the clip, no cycling in between
}

namespace
{
// Random operations on one weapon; the number of rounds in the universe must only ever drop by shots fired.
void RunConservationFuzz(const char* weaponId, bool external, const char* magDef, const std::vector<const char*>& ammoIds, const std::vector<int>& stackSizes, uint64_t seed, int steps)
{
    Env e(4, 4);
    CHECK(e.ok);
    Rng rng(seed);
    WeaponState w;
    CHECK(MakeWeaponState(e.game, weaponId, &w) == WeaponStatus::Ok);
    w.Condition = 12.f;   // worn: jams happen

    std::vector<ItemId> stacks, mags;
    for (size_t i = 0; i < ammoIds.size(); ++i) stacks.push_back(e.Make(ammoIds[i], stackSizes[i]));
    if (external) for (int i = 0; i < 3; ++i) mags.push_back(e.Make(magDef, 1));

    const int startFailures = ScavarTest::Failures();
    const long total = TotalRounds(e, w);
    long fired = 0;
    double now = 0.0;
    int okFires = 0;

    for (int step = 0; step < steps; ++step)
    {
        now += rng.NextUnit() * 1.5;
        switch (rng.RangeInt(0, 9))
        {
        case 0: case 1:
        {
            const auto r = Fire(w, e.store, e.game, now, rng);
            if (r.Status == WeaponStatus::Ok) { ++fired; ++okFires; }
            break;
        }
        case 2:
            if (external) { int n = 0; LoadRoundsIntoMagazine(e.store, e.game, mags[static_cast<size_t>(rng.RangeInt(0, 2))], stacks[static_cast<size_t>(rng.RangeInt(0, (int)stacks.size() - 1))], rng.RangeInt(1, 12), &n); }
            else BeginLoadInternal(w, e.store, e.game, stacks[static_cast<size_t>(rng.RangeInt(0, (int)stacks.size() - 1))], rng.RangeInt(1, 6), now);
            break;
        case 3:
            if (external) UnloadMagazine(e.store, e.game, mags[static_cast<size_t>(rng.RangeInt(0, 2))], e.bag);
            else UnloadWeaponRounds(w, e.store, e.game, e.bag);
            break;
        case 4:
            if (external) BeginReloadMagazine(w, e.store, e.game, mags[static_cast<size_t>(rng.RangeInt(0, 2))], e.slot, e.bag, now);
            else BeginCycle(w, e.store, now);
            break;
        case 5: case 6:
            if (w.Pending.Kind != ActionKind::None) CompleteAction(w, e.store, e.game, now + rng.NextUnit() * 3.0);
            break;
        case 7: CancelAction(w); break;
        case 8: BeginClearJam(w, now); break;
        default:
            if (external) UnloadWeaponRounds(w, e.store, e.game, e.bag); else BeginCycle(w, e.store, now);
            break;
        }
        if (step % 7 == 0 && w.Pending.Kind != ActionKind::None) CompleteAction(w, e.store, e.game, w.Pending.DueAt);

        CHECK_EQ(TotalRounds(e, w) + fired, total);
        if (step % 50 == 0)
        {
            std::string why;
            if (!e.store.CheckInvariants(&why)) { std::printf("    invariant: %s\n", why.c_str()); CHECK(false); }
        }
        if (ScavarTest::Failures() > startFailures) { std::printf("    stopped at step %d (%s)\n", step, weaponId); return; }
    }
    CHECK(okFires > 0);
}
}

TEST(Weapon_RoundsAreConservedAcrossTenThousandRandomOperations)
{
    RunConservationFuzz("W_PM", true, "MAG_PM_8", {"A_918_FMJ", "A_918_AP"}, {60, 30}, 11, 10000);
    RunConservationFuzz("W_MOSIN", false, nullptr, {"A_762R_FMJ", "A_762R_AP"}, {60, 25}, 12, 10000);
    RunConservationFuzz("W_TOZ34", false, nullptr, {"A_12_BUCK", "A_12_SLUG"}, {20, 12}, 13, 10000);
    RunConservationFuzz("W_AKM", true, "MAG_AKM_30", {"A_76239_PS", "A_76239_BP"}, {60, 40}, 14, 10000);
}
