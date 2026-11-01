#include "TestFramework.h"
#include "Inventory.h"
#include "Rng.h"

using namespace Scavar;

namespace
{
ItemCatalog MakeCatalog()
{
    ItemCatalog c;
    c.Register({"AMMO", 1, 1, 0.01f, 60});
    c.Register({"BANDAGE", 1, 1, 0.05f, 4});
    c.Register({"RIFLE", 2, 4, 3.5f, 1});
    c.Register({"MAG", 1, 2, 0.3f, 1});
    c.Register({"BREAD", 1, 1, 0.4f, 5});
    return c;
}

ContainerId AddBox(ItemStore& s, ContainerKind k, int w, int h, int64_t owner = 0)
{
    ContainerId id = 0;
    Status st = s.AddContainer({0, k, w, h, owner}, &id);
    CHECK(st == Status::Ok);
    return id;
}
} // namespace

TEST(Inventory_CatalogRejectsBadDefs)
{
    ItemCatalog c;
    CHECK(c.Register({"A", 1, 1, 0, 1}));
    CHECK(!c.Register({"A", 1, 1, 0, 1}));
    CHECK(!c.Register({"B", 0, 1, 0, 1}));
    CHECK(!c.Register({"C", 1, 1, 0, 0}));
    CHECK(c.Find("A") != nullptr && c.Find("Z") == nullptr);
}

TEST(Inventory_CreateValidatesEverythingBeforeMutating)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId bag = AddBox(s, ContainerKind::Backpack, 4, 4);
    ItemId rifle = 0;
    CHECK(s.Create("RIFLE", 1, bag, 0, 0, false, &rifle) == Status::Ok);
    CHECK(s.Create("NOPE", 1, bag, 3, 0, false) == Status::NoSuchDef);
    CHECK(s.Create("AMMO", 0, bag, 3, 0, false) == Status::InvalidArgument);
    CHECK(s.Create("AMMO", 61, bag, 3, 0, false) == Status::StackLimit);
    CHECK(s.Create("AMMO", 5, 999, 3, 0, false) == Status::NoSuchContainer);
    CHECK(s.Create("AMMO", 5, bag, 4, 0, false) == Status::OutOfBounds);
    CHECK(s.Create("AMMO", 5, bag, -1, 0, false) == Status::OutOfBounds);
    CHECK(s.Create("AMMO", 5, bag, 1, 3, false) == Status::Overlap);      // rifle occupies x0-1,y0-3
    CHECK(s.Create("AMMO", 5, bag, 2, 0, false) == Status::Ok);
    CHECK_EQ(s.AllItems().size(), size_t(2));
    std::string why;
    CHECK(s.CheckInvariants(&why));
}

TEST(Inventory_MoveRespectsBoundsOverlapAndRotation)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId a = AddBox(s, ContainerKind::Backpack, 4, 4);
    ContainerId b = AddBox(s, ContainerKind::Vest, 3, 2);
    ItemId rifle = 0, ammo = 0;
    CHECK(s.Create("RIFLE", 1, a, 0, 0, false, &rifle) == Status::Ok);
    CHECK(s.Create("AMMO", 10, a, 3, 3, false, &ammo) == Status::Ok);
    CHECK(s.Move(rifle, b, 0, 0, false) == Status::OutOfBounds);          // 2x4 does not fit a 3x2 vest
    CHECK(s.Move(rifle, b, 0, 0, true) == Status::OutOfBounds);           // rotated 4x2 is wider than 3
    CHECK(s.Move(ammo, a, 1, 1, false) == Status::Overlap);
    CHECK(s.Move(ammo, a, 0, 0, false) == Status::Overlap);
    CHECK(s.Move(rifle, a, 0, 0, false) == Status::Ok);                   // moving onto itself is fine
    CHECK(s.Move(rifle, a, 0, 0, true) == Status::Ok);                    // rotate in place: now 4 wide, 2 tall
    CHECK_EQ(s.GetItem(rifle)->Rot, true);
    CHECK(s.Move(ammo, a, 0, 1, false) == Status::Overlap);               // rotated rifle covers y0-1
    CHECK(s.Move(ammo, a, 0, 2, false) == Status::Ok);
    CHECK(s.Move(999, a, 0, 0, false) == Status::NoSuchItem);
    CHECK(s.Move(ammo, 999, 0, 0, false) == Status::NoSuchContainer);
    CHECK(s.ItemsIn(a).size() == 2 && s.ItemsIn(b).empty());
    std::string why;
    CHECK(s.CheckInvariants(&why));
}

TEST(Inventory_MergeAndSplit)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId bag = AddBox(s, ContainerKind::Backpack, 5, 5);
    ItemId a = 0, b = 0, bread = 0, c = 0;
    CHECK(s.Create("AMMO", 40, bag, 0, 0, false, &a) == Status::Ok);
    CHECK(s.Create("AMMO", 30, bag, 1, 0, false, &b) == Status::Ok);
    CHECK(s.Create("BREAD", 1, bag, 2, 0, false, &bread) == Status::Ok);
    CHECK(s.Merge(a, b) == Status::StackLimit);                           // 70 > 60, nothing changes
    CHECK_EQ(s.GetItem(a)->Qty, 40);
    CHECK_EQ(s.GetItem(b)->Qty, 30);
    CHECK(s.Merge(a, bread) == Status::WrongDef);
    CHECK(s.Merge(a, a) == Status::InvalidArgument);
    CHECK(s.Merge(a, b, 30) == Status::Ok);                               // partial: 10 / 60
    CHECK_EQ(s.GetItem(a)->Qty, 10);
    CHECK_EQ(s.GetItem(b)->Qty, 60);
    CHECK(s.Merge(a, b, 1) == Status::StackLimit);
    CHECK(s.Split(a, 10, bag, 0, 1, false) == Status::InvalidArgument);   // would leave an empty stack
    CHECK(s.Split(a, 4, bag, 0, 0, false) == Status::Overlap);            // source still sits at 0,0
    CHECK(s.Split(a, 4, bag, 0, 1, false, &c) == Status::Ok);
    CHECK_EQ(s.GetItem(a)->Qty, 6);
    CHECK_EQ(s.GetItem(c)->Qty, 4);
    CHECK(s.Merge(c, a) == Status::Ok);                                   // full merge removes the source
    CHECK(s.GetItem(c) == nullptr);
    CHECK_EQ(s.GetItem(a)->Qty, 10);
    CHECK_EQ(s.TotalQtyByDef().at("AMMO"), int64_t(70));
    std::string why;
    CHECK(s.CheckInvariants(&why));
}

TEST(Inventory_AutoPlaceUsesRotationAndReportsNoSpace)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId strip = AddBox(s, ContainerKind::Vest, 4, 2);             // only fits a rifle rotated
    Placement p;
    CHECK(s.FindFreeSlot("RIFLE", strip, false, 0, &p) == Status::NoSpace);
    CHECK(s.FindFreeSlot("RIFLE", strip, true, 0, &p) == Status::Ok && p.Rot);
    ItemId id = 0;
    CHECK(s.AutoPlace("RIFLE", 1, strip, true, &id) == Status::Ok);
    CHECK(s.AutoPlace("RIFLE", 1, strip, true) == Status::NoSpace);
    CHECK(s.AutoPlace("AMMO", 1, strip, true) == Status::NoSpace);        // rifle fills the whole 4x2
    CHECK_EQ(s.AllItems().size(), size_t(1));
}

TEST(AT007_DoublePickupHasExactlyOneWinner)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId ground = AddBox(s, ContainerKind::Ground, 2, 2);
    ContainerId bagA = AddBox(s, ContainerKind::Backpack, 4, 4, 1);
    ContainerId bagB = AddBox(s, ContainerKind::Backpack, 4, 4, 2);
    ItemId loot = 0;
    CHECK(s.Create("BANDAGE", 2, ground, 0, 0, false, &loot) == Status::Ok);
    const Status first = s.AutoMove(loot, bagA, true, ground);
    const Status second = s.AutoMove(loot, bagB, true, ground);           // both requests name the ground as the source
    CHECK(first == Status::Ok);
    CHECK(second == Status::WrongSource);
    CHECK(s.ItemsIn(bagA).size() == 1 && s.ItemsIn(bagB).empty() && s.ItemsIn(ground).empty());
    CHECK_EQ(s.TotalQtyByDef().at("BANDAGE"), int64_t(2));
}

TEST(AT008_PickupOverflowLeavesItemOnGround)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId ground = AddBox(s, ContainerKind::Ground, 2, 2);
    ContainerId tiny = AddBox(s, ContainerKind::Pockets, 1, 1);
    ItemId a = 0, b = 0;
    CHECK(s.Create("BREAD", 1, ground, 0, 0, false, &a) == Status::Ok);
    CHECK(s.Create("BREAD", 1, ground, 1, 0, false, &b) == Status::Ok);
    CHECK(s.AutoMove(a, tiny, true, ground) == Status::Ok);
    CHECK(s.AutoMove(b, tiny, true, ground) == Status::NoSpace);
    CHECK_EQ(s.GetItem(b)->Container, ground);                            // still lootable, not lost
    CHECK_EQ(s.TotalQtyByDef().at("BREAD"), int64_t(2));
}

TEST(Inventory_WeightAndContainerLifecycle)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId bag = AddBox(s, ContainerKind::Backpack, 4, 4);
    ItemId rifle = 0;
    s.Create("RIFLE", 1, bag, 0, 0, false, &rifle);
    s.Create("AMMO", 30, bag, 2, 0, false);
    CHECK_NEAR(s.ContainerWeight(bag), 3.5 + 0.3, 1e-5);
    CHECK(s.RemoveContainer(bag) == Status::NotEmpty);
    CHECK(s.RemoveContainer(999) == Status::NoSuchContainer);
    CHECK(s.AddContainer({bag, ContainerKind::Ground, 1, 1, 0}) == Status::AlreadyExists);
    CHECK(s.AddContainer({0, ContainerKind::Ground, 0, 1, 0}) == Status::InvalidArgument);
    CHECK(s.Remove(rifle) == Status::Ok);
    CHECK(s.Remove(rifle) == Status::NoSuchItem);
}

TEST(Inventory_RollbackRestoresExactStateIncludingContainers)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId a = AddBox(s, ContainerKind::Backpack, 4, 4);
    ContainerId b = AddBox(s, ContainerKind::Stash, 4, 4);
    ItemId x = 0;
    s.Create("AMMO", 50, a, 0, 0, false, &x);
    s.Create("BREAD", 3, a, 1, 0, false);
    s.DrainChanges();
    const ItemStore before = s;

    s.BeginTx();
    ContainerId c = AddBox(s, ContainerKind::Ground, 2, 2);
    CHECK(s.Move(x, c, 0, 0, false) == Status::Ok);
    CHECK(s.Split(x, 10, b, 0, 0, false) == Status::Ok);
    CHECK(s.Remove(s.ItemsIn(a).front()) == Status::Ok);
    CHECK(s.SetContainerKind(b, ContainerKind::Corpse) == Status::Ok);
    CHECK(!s.Equals(before));
    CHECK(s.Rollback() == Status::Ok);
    CHECK(s.Equals(before));
    CHECK(s.CheckInvariants(nullptr));
    CHECK(s.Rollback() == Status::InvalidArgument);                       // no open tx
}

TEST(Inventory_NestedTransactions)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId a = AddBox(s, ContainerKind::Backpack, 4, 4);
    s.DrainChanges();
    s.BeginTx();
    CHECK(s.Create("AMMO", 1, a, 0, 0, false) == Status::Ok);
    s.BeginTx();
    CHECK(s.Create("AMMO", 1, a, 1, 0, false) == Status::Ok);
    CHECK(s.Rollback() == Status::Ok);                                    // inner only
    CHECK_EQ(s.AllItems().size(), size_t(1));
    s.BeginTx();
    CHECK(s.Create("AMMO", 1, a, 2, 0, false) == Status::Ok);
    CHECK(s.Commit() == Status::Ok);                                      // inner commit stays undoable by outer
    CHECK_EQ(s.AllItems().size(), size_t(2));
    CHECK(s.Rollback() == Status::Ok);                                    // outer
    CHECK(s.AllItems().empty());
    CHECK(!s.InTx());
}

TEST(Inventory_DrainChangesCoalescesAndWaitsForTxEnd)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId a = AddBox(s, ContainerKind::Backpack, 4, 4);
    ItemId x = 0, y = 0;
    s.Create("AMMO", 5, a, 0, 0, false, &x);
    s.Create("AMMO", 5, a, 1, 0, false, &y);
    s.Move(x, a, 2, 2, false);
    s.Remove(y);
    s.BeginTx();
    CHECK(s.DrainChanges().Empty());                                      // not while a tx is open
    CHECK(s.HasPendingChanges());
    s.Commit();
    ChangeSet cs = s.DrainChanges();
    CHECK_EQ(cs.Containers.size(), size_t(1));
    CHECK_EQ(cs.Items.size(), size_t(2));
    for (const ItemChange& ic : cs.Items)
    {
        if (ic.Id == x) { CHECK(ic.After.has_value() && ic.After->X == 2 && ic.After->Y == 2); }
        else { CHECK(!ic.After.has_value()); }
    }
    CHECK(s.DrainChanges().Empty());
}

TEST(AT035_RandomOperationsKeepInvariantsAndConserveQuantity)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    Rng rng(0xA735);
    std::vector<ContainerId> boxes;
    for (int i = 0; i < 12; ++i)
    {
        const int w = 2 + rng.RangeInt(0, 6), h = 2 + rng.RangeInt(0, 6);
        boxes.push_back(AddBox(s, i % 3 == 0 ? ContainerKind::Ground : ContainerKind::Backpack, w, h, i));
    }
    const char* defs[] = {"AMMO", "BANDAGE", "RIFLE", "MAG", "BREAD"};
    const int maxQty[] = {60, 4, 1, 1, 5};
    for (int i = 0; i < 160; ++i)
    {
        const int d = rng.RangeInt(0, 4);
        s.AutoPlace(defs[d], rng.RangeInt(1, maxQty[d]), boxes[rng.RangeInt(0, 11)], true);
    }
    const auto expected = s.TotalQtyByDef();
    CHECK(s.AllItems().size() > 40);
    s.DrainChanges();

    auto randomItem = [&]() -> ItemId {
        if (s.AllItems().empty()) return 0;
        auto it = s.AllItems().begin();
        std::advance(it, rng.RangeInt(0, static_cast<int>(s.AllItems().size()) - 1));
        return it->first;
    };

    int okMove = 0, okMerge = 0, okSplit = 0, okAuto = 0, rollbacks = 0, commits = 0, snapshotChecks = 0;
    for (int op = 0; op < 100000; ++op)
    {
        const bool useTx = rng.Chance(0.4);
        const bool snapshot = useTx && op % 250 == 0;
        ItemStore saved(&cat);
        if (snapshot) saved = s;
        if (useTx) s.BeginTx();

        const int steps = useTx ? rng.RangeInt(1, 4) : 1;
        for (int k = 0; k < steps; ++k)
        {
            const ItemId id = randomItem();
            switch (rng.RangeInt(0, 3))
            {
            case 0: {
                ContainerId dst = boxes[rng.RangeInt(0, 11)];
                const Container* c = s.GetContainer(dst);
                okMove += s.Move(id, dst, rng.RangeInt(0, c->W - 1), rng.RangeInt(0, c->H - 1), rng.Chance(0.3)) == Status::Ok;
                break; }
            case 1:
                okMerge += s.Merge(id, randomItem(), rng.Chance(0.5) ? 0 : rng.RangeInt(1, 10)) == Status::Ok;
                break;
            case 2: {
                ContainerId dst = boxes[rng.RangeInt(0, 11)];
                const Container* c = s.GetContainer(dst);
                okSplit += s.Split(id, rng.RangeInt(1, 30), dst, rng.RangeInt(0, c->W - 1), rng.RangeInt(0, c->H - 1), rng.Chance(0.3)) == Status::Ok;
                break; }
            default:
                okAuto += s.AutoMove(id, boxes[rng.RangeInt(0, 11)], true) == Status::Ok;
                break;
            }
        }
        if (useTx)
        {
            if (rng.Chance(0.5)) { s.Rollback(); ++rollbacks; if (snapshot) { CHECK(s.Equals(saved)); ++snapshotChecks; } }
            else { s.Commit(); ++commits; }
        }
        if (op % 1000 == 0)
        {
            std::string why;
            if (!s.CheckInvariants(&why)) { std::printf("    invariant broken at op %d: %s\n", op, why.c_str()); CHECK(false); return; }
            CHECK(s.TotalQtyByDef() == expected);
        }
    }
    std::string why;
    CHECK(s.CheckInvariants(&why));
    CHECK(s.TotalQtyByDef() == expected);
    std::printf("    ok moves=%d merges=%d splits=%d auto=%d commits=%d rollbacks=%d snapshots=%d\n", okMove, okMerge, okSplit, okAuto, commits, rollbacks, snapshotChecks);
    CHECK(okMove > 3000 && okMerge > 500 && okSplit > 500 && okAuto > 2000);
    CHECK(rollbacks > 5000 && commits > 5000 && snapshotChecks > 20);
}

TEST(AT036_DeathResolutionIsAtomicAndLosesNothing)
{
    ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId pockets = AddBox(s, ContainerKind::Pockets, 4, 1, 7);
    ContainerId backpack = AddBox(s, ContainerKind::Backpack, 5, 5, 7);
    ContainerId secure = AddBox(s, ContainerKind::Secure, 2, 2, 7);
    ContainerId returns = AddBox(s, ContainerKind::Returns, 4, 4, 100);
    CHECK(s.Create("AMMO", 30, pockets, 0, 0, false) == Status::Ok);
    CHECK(s.Create("RIFLE", 1, backpack, 0, 0, false) == Status::Ok);
    CHECK(s.Create("BREAD", 2, secure, 0, 0, false) == Status::Ok);
    CHECK(s.Create("BANDAGE", 3, secure, 1, 0, false) == Status::Ok);
    CHECK(s.Create("AMMO", 20, secure, 0, 1, false) == Status::Ok);
    s.DrainChanges();
    const auto totals = s.TotalQtyByDef();

    // Failure path first: a returns container that is too small must leave everything untouched.
    ContainerId tinyReturns = AddBox(s, ContainerKind::Returns, 1, 1, 100);
    const ItemStore snapshot = s;
    CHECK(s.ResolveDeath(secure, tinyReturns, {pockets, backpack}) == Status::NoSpace);
    CHECK(s.Equals(snapshot));
    CHECK(!s.InTx());
    CHECK(s.ResolveDeath(pockets, returns, {backpack}) == Status::WrongKind);
    CHECK(s.ResolveDeath(secure, secure, {backpack}) == Status::WrongKind);
    CHECK(s.ResolveDeath(secure, returns, {secure}) == Status::InvalidArgument);
    CHECK(s.ResolveDeath(secure, returns, {12345}) == Status::NoSuchContainer);
    CHECK(s.Equals(snapshot));

    CHECK(s.ResolveDeath(secure, returns, {pockets, backpack}) == Status::Ok);
    CHECK(s.GetContainer(secure) == nullptr);
    CHECK_EQ(s.ItemsIn(returns).size(), size_t(3));
    CHECK(s.GetContainer(pockets)->Kind == ContainerKind::Corpse);
    CHECK(s.GetContainer(backpack)->Kind == ContainerKind::Corpse);
    CHECK_EQ(s.ItemsIn(pockets).size(), size_t(1));                       // body loot stays lootable
    CHECK_EQ(s.ItemsIn(backpack).size(), size_t(1));
    CHECK(s.TotalQtyByDef() == totals);                                   // nothing duplicated, nothing lost
    std::string why;
    CHECK(s.CheckInvariants(&why));
    CHECK(s.ResolveDeath(secure, returns, {pockets}) == Status::NoSuchContainer);   // cannot resolve twice
}
