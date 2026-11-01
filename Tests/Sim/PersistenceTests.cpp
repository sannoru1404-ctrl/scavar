#include "TestFramework.h"
#include "Db.h"
#include "Inventory.h"
#include "Rng.h"
#include <sqlite3.h>
#include <unistd.h>
#include <filesystem>

using namespace Scavar;
namespace fs = std::filesystem;

namespace
{
ItemCatalog MakeCatalog()
{
    ItemCatalog c;
    c.Register({"AMMO", 1, 1, 0.01f, 60});
    c.Register({"BANDAGE", 1, 1, 0.05f, 4});
    c.Register({"RIFLE", 2, 4, 3.5f, 1});
    c.Register({"BREAD", 1, 1, 0.4f, 5});
    return c;
}

struct TempDir
{
    fs::path Path;
    TempDir()
    {
        static int counter = 0;
        std::error_code ec;
        Path = fs::temp_directory_path(ec) / ("scavar_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        fs::remove_all(Path, ec);
        fs::create_directories(Path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(Path, ec); }
    std::string File(const char* name) const { return (Path / name).string(); }
};

ContainerId Box(ItemStore& s, ContainerKind k, int w, int h, int64_t owner = 0)
{
    ContainerId id = 0;
    CHECK(s.AddContainer({0, k, w, h, owner}, &id) == Status::Ok);
    return id;
}

void Seed(ItemStore& s, ContainerId* bag = nullptr, ContainerId* stash = nullptr)
{
    ContainerId b = Box(s, ContainerKind::Backpack, 5, 5, 1);
    ContainerId st = Box(s, ContainerKind::Stash, 8, 10, 1);
    CHECK(s.Create("AMMO", 40, b, 0, 0, false) == Status::Ok);
    CHECK(s.Create("RIFLE", 1, b, 1, 0, true, nullptr, 87.5f) == Status::Ok);
    CHECK(s.Create("BREAD", 3, st, 2, 3, false) == Status::Ok);
    if (bag) *bag = b;
    if (stash) *stash = st;
}
} // namespace

TEST(Db_FreshDatabaseHasSchemaV1AndAllTables)
{
    TempDir dir; Db db; std::string err;
    CHECK(db.Open(dir.File("a.sqlite"), &err));
    CHECK_EQ(db.SchemaVersion(), 1);
    const char* tables[] = {"accounts", "characters", "containers", "items", "world_state", "trader_state", "account_progress", "tasks", "ledger"};
    for (const char* t : tables)
    {
        int64_t n = 0;
        CHECK(db.ScalarInt(std::string("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='") + t + "'", &n, &err));
        CHECK_EQ(n, int64_t(1));
    }
    int64_t fk = 0;
    CHECK(db.ScalarInt("PRAGMA foreign_keys", &fk, &err));
    CHECK_EQ(fk, int64_t(1));
    db.Close();
    CHECK(db.Open(dir.File("a.sqlite"), &err));                           // reopening is a no-op migration
    CHECK_EQ(db.SchemaVersion(), 1);
}

TEST(Db_RefusesNewerSchemaAndUnversionedPopulatedDatabases)
{
    TempDir dir; Db db; std::string err;
    CHECK(db.Open(dir.File("n.sqlite"), &err));
    CHECK(db.Exec("PRAGMA user_version=99", &err));
    db.Close();
    CHECK(!db.Open(dir.File("n.sqlite"), &err));
    CHECK(err.find("newer") != std::string::npos);
    CHECK(!db.IsOpen());

    sqlite3* raw = nullptr;                                               // a foreign database: tables but version 0
    CHECK(sqlite3_open(dir.File("f.sqlite").c_str(), &raw) == SQLITE_OK);
    CHECK(sqlite3_exec(raw, "CREATE TABLE stuff(x)", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);
    CHECK(!db.Open(dir.File("f.sqlite"), &err));
}

TEST(Db_RoundTripStoreToDbAndBack)
{
    TempDir dir; ItemCatalog cat = MakeCatalog();
    ItemStore a(&cat);
    ContainerId bag = 0, stash = 0;
    Seed(a, &bag, &stash);
    ItemId extra = 0;
    CHECK(a.Split(a.ItemsIn(bag).front(), 10, stash, 0, 0, false, &extra) == Status::Ok);
    CHECK(a.SetFlags(extra, 0xA5A5u) == Status::Ok);
    CHECK(a.SetAux(extra, 7) == Status::Ok);

    Db db; std::string err;
    CHECK(db.Open(dir.File("rt.sqlite"), &err));
    CHECK(db.Apply(a.DrainChanges(), &err));
    ItemStore b(&cat);
    CHECK(db.LoadInto(b, &err));
    CHECK(a.Equals(b));
    CHECK_EQ(b.NextItemId(), a.NextItemId());
    CHECK_EQ(b.NextContainerId(), a.NextContainerId());
    CHECK_EQ(b.GetItem(extra)->Flags, 0xA5A5u);
    CHECK_EQ(b.GetItem(extra)->Aux, 7);
    CHECK_NEAR(b.ContainerWeight(bag), a.ContainerWeight(bag), 1e-6);

    // Incremental changes: move, delete, container delete.
    CHECK(a.Remove(extra) == Status::Ok);
    ContainerId trash = Box(a, ContainerKind::Ground, 2, 2);
    CHECK(a.RemoveContainer(trash) == Status::Ok);
    CHECK(db.Apply(a.DrainChanges(), &err));
    ItemStore c(&cat);
    db.Close();
    CHECK(db.Open(dir.File("rt.sqlite"), &err));
    CHECK(db.LoadInto(c, &err));
    CHECK(a.Equals(c));
    CHECK(db.CheckInvariants(&cat, &err));
}

TEST(Db_ApplyIsAllOrNothing)
{
    TempDir dir; ItemCatalog cat = MakeCatalog();
    ItemStore a(&cat);
    Seed(a);
    Db db; std::string err;
    CHECK(db.Open(dir.File("atomic.sqlite"), &err));
    CHECK(db.Apply(a.DrainChanges(), &err));
    std::map<std::string, int64_t> before;
    CHECK(db.TotalQtyByDef(&before, &err));

    ChangeSet bad;                                                        // second change violates the container foreign key
    ItemChange ok; ok.Id = 900; ItemInstance good; good.Id = 900; good.DefId = "AMMO"; good.Qty = 5; good.Container = a.AllContainers().begin()->first; good.X = 4; good.Y = 4;
    ok.After = good;
    ItemChange broken; broken.Id = 901; ItemInstance orphan = good; orphan.Id = 901; orphan.Container = 424242; orphan.X = 0;
    broken.After = orphan;
    bad.Items = {ok, broken};
    CHECK(!db.Apply(bad, &err));
    CHECK(!err.empty());
    std::map<std::string, int64_t> after;
    CHECK(db.TotalQtyByDef(&after, &err));
    CHECK(before == after);                                               // item 900 was rolled back too
    int64_t n = 0;
    CHECK(db.ScalarInt("SELECT COUNT(*) FROM items WHERE id=900", &n, &err));
    CHECK_EQ(n, int64_t(0));
}

TEST(Db_LedgerMakesTransactionsIdempotent)
{
    TempDir dir; ItemCatalog cat = MakeCatalog();
    Db db; std::string err;
    CHECK(db.Open(dir.File("ledger.sqlite"), &err));
    CHECK(db.TryRecordTx("tx-1", "buy", 5, "{}", &err));
    CHECK(!db.TryRecordTx("tx-1", "buy", 5, "{}", &err));
    CHECK(err.empty());                                                   // duplicate is not an error
    CHECK(db.TryRecordTx("tx-2", "sell", 5, "{}", &err));

    ItemStore s(&cat);
    ContainerId bag = Box(s, ContainerKind::Backpack, 4, 4);
    CHECK(s.Create("AMMO", 10, bag, 0, 0, false) == Status::Ok);
    ChangeSet first = s.DrainChanges();
    CHECK(db.ApplyWithTx(first, "tx-3", "loot", 5, "{}", &err) == Db::TxResult::Applied);
    CHECK(s.Create("AMMO", 10, bag, 1, 0, false) == Status::Ok);
    ChangeSet second = s.DrainChanges();
    CHECK(db.ApplyWithTx(second, "tx-3", "loot", 5, "{}", &err) == Db::TxResult::Duplicate);   // same tx id: nothing applied
    std::map<std::string, int64_t> totals;
    CHECK(db.TotalQtyByDef(&totals, &err));
    CHECK_EQ(totals["AMMO"], int64_t(10));
    CHECK(db.ApplyWithTx(second, "tx-4", "loot", 5, "{}", &err) == Db::TxResult::Applied);
    CHECK(db.TotalQtyByDef(&totals, &err));
    CHECK_EQ(totals["AMMO"], int64_t(20));

    ChangeSet bad;                                                        // a failed apply must not burn the tx id
    ItemChange c; c.Id = 77; ItemInstance i; i.Id = 77; i.DefId = "AMMO"; i.Container = 31337; c.After = i;
    bad.Items = {c};
    CHECK(db.ApplyWithTx(bad, "tx-5", "loot", 5, "{}", &err) == Db::TxResult::Error);
    CHECK(db.TryRecordTx("tx-5", "loot", 5, "{}", &err));
}

TEST(Db_InvariantCheckDetectsTamperingAndConservationBreaks)
{
    TempDir dir; ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    Seed(s);
    Db db; std::string err;
    CHECK(db.Open(dir.File("inv.sqlite"), &err));
    CHECK(db.Apply(s.DrainChanges(), &err));
    CHECK(db.CheckInvariants(&cat, &err));
    CHECK(db.VerifyConservation(s.TotalQtyByDef(), &err));

    auto wrong = s.TotalQtyByDef();
    wrong["AMMO"] += 1;
    CHECK(!db.VerifyConservation(wrong, &err));
    CHECK(err.find("AMMO") != std::string::npos);

    // Overlap that SQL alone cannot see: caught by the loaded-store check.
    int64_t bag = s.AllContainers().begin()->first;
    CHECK(db.Exec("INSERT INTO items(id,def_id,qty,container_id,x,y) VALUES(500,'AMMO',1," + std::to_string(bag) + ",0,0)", &err));
    CHECK(!db.CheckInvariants(&cat, &err));
    CHECK(err.find("overlap") != std::string::npos);
    CHECK(db.Exec("DELETE FROM items WHERE id=500", &err));
    CHECK(db.CheckInvariants(&cat, &err));

    // Orphan item via disabled foreign keys.
    CHECK(db.Exec("PRAGMA foreign_keys=OFF", &err));
    CHECK(db.Exec("INSERT INTO items(id,def_id,qty,container_id,x,y) VALUES(501,'AMMO',1,9999,0,0)", &err));
    CHECK(!db.CheckInvariants(nullptr, &err));
}

TEST(Db_BackupToDirKeepsNewestAndRestoresIdentically)
{
    TempDir dir; ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    Seed(s);
    Db db; std::string err;
    CHECK(db.Open(dir.File("live.sqlite"), &err));
    CHECK(db.Apply(s.DrainChanges(), &err));
    std::string path;
    for (int h = 0; h < 4; ++h)
        CHECK(db.BackupToDir((dir.Path / "backups").string(), 1700000000 + h * 3600, 2, &path, &err));
    int count = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir.Path / "backups", ec)) { (void)e; ++count; }
    CHECK_EQ(count, 2);
    CHECK(path.find("backup_001700010800.sqlite") != std::string::npos);

    Db restored; ItemStore r(&cat);
    CHECK(restored.Open(path, &err));
    CHECK(restored.LoadInto(r, &err));
    CHECK(r.Equals(s));
}

TEST(Db_EnsureAccountIsStable)
{
    TempDir dir; Db db; std::string err;
    CHECK(db.Open(dir.File("acc.sqlite"), &err));
    AccountId a = 0, b = 0, c = 0;
    CHECK(db.EnsureAccount("steam:1", &a, &err));
    CHECK(db.EnsureAccount("steam:1", &b, &err));
    CHECK(db.EnsureAccount("steam:2", &c, &err));
    CHECK(a == b && a != c && a > 0);
}

TEST(AT036_DeathResolutionSurvivesPersistenceRoundTrip)
{
    TempDir dir; ItemCatalog cat = MakeCatalog();
    ItemStore s(&cat);
    ContainerId bag = Box(s, ContainerKind::Backpack, 5, 5, 7);
    ContainerId secure = Box(s, ContainerKind::Secure, 2, 2, 7);
    ContainerId returns = Box(s, ContainerKind::Returns, 6, 6, 100);
    CHECK(s.Create("RIFLE", 1, bag, 0, 0, false) == Status::Ok);
    CHECK(s.Create("BREAD", 2, secure, 0, 0, false) == Status::Ok);
    Db db; std::string err;
    CHECK(db.Open(dir.File("death.sqlite"), &err));
    CHECK(db.Apply(s.DrainChanges(), &err));
    const auto totals = s.TotalQtyByDef();

    CHECK(s.ResolveDeath(secure, returns, {bag}) == Status::Ok);
    CHECK(db.ApplyWithTx(s.DrainChanges(), "death-7", "death", 7, "{}", &err) == Db::TxResult::Applied);
    CHECK(db.VerifyConservation(totals, &err));
    CHECK(db.CheckInvariants(&cat, &err));
    ItemStore r(&cat);
    CHECK(db.LoadInto(r, &err));
    CHECK(r.Equals(s));
    CHECK(r.GetContainer(secure) == nullptr);
    CHECK(r.GetContainer(bag)->Kind == ContainerKind::Corpse);
}
