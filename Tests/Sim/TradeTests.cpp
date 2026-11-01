#include "TestFramework.h"
#include "Db.h"
#include "DbLedger.h"
#include "Tasks.h"
#include "Trade.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

using namespace Scavar;

namespace
{
struct Fixture
{
    GameData Game;
    ItemTable Table;
    ItemCatalog Catalog;
    ItemStore Store{&Catalog};
    ContainerId Pockets = 0, Backpack = 0;
    std::vector<ContainerId> Wallet;
    MemoryLedger Memory;

    bool Init(int64_t rubles = 1000, int packW = 4, int packH = 5)
    {
        std::string err;
        if (!Game.Load(SCAVAR_DATA_DIR, &err) || !Table.Load(SCAVAR_DATA_DIR, Game, &err) || !Table.BuildCatalog(Catalog))
        {
            std::printf("    fixture error: %s\n", err.c_str());
            return false;
        }
        Container pk; pk.Kind = ContainerKind::Pockets; pk.W = 4; pk.H = 1;
        Container bp; bp.Kind = ContainerKind::Backpack; bp.W = packW; bp.H = packH;
        if (Store.AddContainer(pk, &Pockets) != Status::Ok || Store.AddContainer(bp, &Backpack) != Status::Ok) return false;
        Wallet = {Pockets, Backpack};
        if (rubles > 0 && Store.AutoPlace(kMoneyDef, static_cast<int>(rubles), Backpack, true) != Status::Ok) return false;
        return true;
    }
    int64_t Qty(TradeEngine& e, const char* def) { return e.QtyOf(Wallet, def); }
};
}

TEST(AT027_Trade_BuyMovesMoneyAndItemTogether)
{
    Fixture f; CHECK(f.Init(1000));
    TradeEngine engine(f.Store, f.Table, f.Memory);
    TraderStockState stock; stock.Entries.push_back(MakeBasicEntry("MED_BANDAGE"));
    TraderProgress progress;
    const int64_t unit = engine.BuyUnitPrice("MED_BANDAGE", 1);
    CHECK_EQ(unit, int64_t(78));   // 60 * 1.30

    const TradeResult r = engine.Buy(1, "buy:1", f.Wallet, stock, progress, "MED_BANDAGE", 2);
    CHECK(r.Ok());
    CHECK_EQ(r.Amount, int64_t(156));
    CHECK_EQ(engine.MoneyIn(f.Wallet), int64_t(844));
    CHECK_EQ(f.Qty(engine, "MED_BANDAGE"), int64_t(2));
    CHECK_EQ(progress.Turnover, int64_t(156));
    std::string why;
    CHECK(f.Store.CheckInvariants(&why));
}

TEST(AT027_Trade_FailuresChangeNothing)
{
    Fixture f; CHECK(f.Init(1000));
    TradeEngine engine(f.Store, f.Table, f.Memory);
    TraderStockState stock;
    stock.Entries.push_back(MakeBasicEntry("MED_BANDAGE"));
    stock.Entries.push_back(MakeBasicEntry("W_MOSIN"));
    TraderProgress progress;
    const ItemStore before = f.Store;

    CHECK(engine.Buy(1, "t1", f.Wallet, stock, progress, "W_MOSIN", 1).Status == TradeStatus::InsufficientFunds);   // 6000 * 1.3
    CHECK(engine.Buy(1, "t2", f.Wallet, stock, progress, "NO_SUCH_ITEM", 1).Status == TradeStatus::UnknownItem);
    CHECK(engine.Buy(1, "t3", f.Wallet, stock, progress, "FOOD_BREAD", 1).Status == TradeStatus::NotForSale);
    CHECK(engine.Buy(1, "t4", f.Wallet, stock, progress, "MED_BANDAGE", 0).Status == TradeStatus::InvalidRequest);
    CHECK(engine.Buy(1, "bad id!", f.Wallet, stock, progress, "MED_BANDAGE", 1).Status == TradeStatus::InvalidRequest);
    CHECK(f.Store.Equals(before));
    CHECK_EQ(f.Memory.Size(), size_t(0));   // failed attempts must not burn tx ids
    CHECK_EQ(progress.Turnover, int64_t(0));
}

TEST(AT027_Trade_NoSpaceRollsBackPayment)
{
    // The only container is 1x1 and holds the money: after paying, the remainder stack occupies the slot, so the item cannot fit.
    Fixture f;
    std::string err;
    CHECK(f.Game.Load(SCAVAR_DATA_DIR, &err) && f.Table.Load(SCAVAR_DATA_DIR, f.Game, &err) && f.Table.BuildCatalog(f.Catalog));
    Container tiny; tiny.Kind = ContainerKind::Pockets; tiny.W = 1; tiny.H = 1;
    ContainerId id = 0;
    CHECK(f.Store.AddContainer(tiny, &id) == Status::Ok);
    CHECK(f.Store.AutoPlace(kMoneyDef, 500, id, false) == Status::Ok);
    const std::vector<ContainerId> wallet{id};
    TradeEngine engine(f.Store, f.Table, f.Memory);
    TraderStockState stock; stock.Entries.push_back(MakeBasicEntry("MED_BANDAGE"));
    TraderProgress progress;
    const ItemStore before = f.Store;

    const TradeResult r = engine.Buy(1, "buy:space", wallet, stock, progress, "MED_BANDAGE", 1);
    CHECK(r.Status == TradeStatus::NoSpace);
    CHECK(f.Store.Equals(before));
    CHECK_EQ(engine.MoneyIn(wallet), int64_t(500));
    CHECK_EQ(f.Memory.Size(), size_t(0));
}

TEST(AT028_Trade_ReplayedTxIsIgnored)
{
    Fixture f; CHECK(f.Init(1000));
    TradeEngine engine(f.Store, f.Table, f.Memory);
    Rng rng(3);
    TraderStockState stock; stock.Entries.push_back(MakeScarceEntry("MED_TOURNIQUET", 0, rng));
    const int stockBefore = stock.Entries[0].Stock;
    CHECK(stockBefore >= 1);
    TraderProgress progress;

    CHECK(engine.Buy(1, "buy:replay", f.Wallet, stock, progress, "MED_TOURNIQUET", 1).Ok());
    const int64_t moneyAfter = engine.MoneyIn(f.Wallet);
    const TradeResult again = engine.Buy(1, "buy:replay", f.Wallet, stock, progress, "MED_TOURNIQUET", 1);
    CHECK(again.Status == TradeStatus::Duplicate);
    CHECK_EQ(engine.MoneyIn(f.Wallet), moneyAfter);
    CHECK_EQ(f.Qty(engine, "MED_TOURNIQUET"), int64_t(1));
    CHECK_EQ(stock.Entries[0].Stock, stockBefore - 1);     // stock consumed once
    CHECK_EQ(progress.Turnover, int64_t(195));              // 150 * 1.30
}

TEST(Trade_ScarceStockLimitsPurchases)
{
    Fixture f; CHECK(f.Init(100000));
    TradeEngine engine(f.Store, f.Table, f.Memory);
    TraderStockState stock;
    StockEntry e; e.DefId = "MED_TOURNIQUET"; e.Scarce = true; e.Stock = 1;
    stock.Entries.push_back(e);
    TraderProgress progress;
    CHECK(engine.Buy(1, "s1", f.Wallet, stock, progress, "MED_TOURNIQUET", 1).Ok());
    CHECK(engine.Buy(1, "s2", f.Wallet, stock, progress, "MED_TOURNIQUET", 1).Status == TradeStatus::OutOfStock);
    CHECK_EQ(f.Qty(engine, "MED_TOURNIQUET"), int64_t(1));
}

TEST(AT029_Trade_SellNeverExceedsBuyAndRoundTripLosesHalf)
{
    Fixture f; CHECK(f.Init(0));
    TradeEngine engine(f.Store, f.Table, f.Memory);
    for (const ItemRow& row : f.Table.Rows)
        for (int buyL = 1; buyL <= 3; ++buyL)
            for (int sellL = 1; sellL <= 3; ++sellL)
            {
                const int64_t buy = engine.BuyUnitPrice(row.Id, buyL), sell = engine.SellUnitPrice(row.Id, sellL);
                CHECK(sell <= buy);
                CHECK(sell * 2 <= buy);
            }
    for (int64_t base = 1; base <= 3000; ++base)
        for (int buyL = 1; buyL <= 3; ++buyL)
            for (int sellL = 1; sellL <= 3; ++sellL)
                if (!(SellPrice(base, sellL) * 2 <= BuyPrice(base, buyL))) { CHECK(false); return; }
}

TEST(Trade_SellPaysAndPartialStacksWork)
{
    Fixture f; CHECK(f.Init(0));
    TradeEngine engine(f.Store, f.Table, f.Memory);
    ItemId ammo = 0;
    CHECK(f.Store.AutoPlace("A_762R_FMJ", 30, f.Backpack, true, &ammo) == Status::Ok);
    TraderProgress progress; progress.Loyalty = 3;
    const int64_t unit = engine.SellUnitPrice("A_762R_FMJ", 3);
    CHECK_EQ(unit, int64_t(15));   // 35 * 0.45 rounded down

    const TradeResult r = engine.Sell(1, "sell:1", f.Wallet, progress, ammo, 10);
    CHECK(r.Ok());
    CHECK_EQ(r.Amount, unit * 10);
    CHECK_EQ(engine.MoneyIn(f.Wallet), unit * 10);
    CHECK_EQ(f.Qty(engine, "A_762R_FMJ"), int64_t(20));
    CHECK(f.Store.GetItem(ammo) != nullptr);                  // the stack keeps its id after a partial sale
    CHECK(engine.Sell(1, "sell:bad", f.Wallet, progress, ammo, 21).Status == TradeStatus::InvalidRequest);
    CHECK(engine.Sell(1, "sell:money", f.Wallet, progress, ammo + 9999, 1).Status == TradeStatus::UnknownItem);
    std::string why;
    CHECK(f.Store.CheckInvariants(&why));
}

TEST(Trade_MoneyRespectsStackLimit)
{
    Fixture f; CHECK(f.Init(0, 6, 8));
    TradeEngine engine(f.Store, f.Table, f.Memory);
    ExchangeRequest req; req.TxId = "grant:1"; req.Kind = "grant"; req.Account = 1; req.Wallet = f.Wallet;
    req.Gives.push_back({kMoneyDef, 100000, 100.f});
    req.Gives.push_back({kMoneyDef, 50000, 100.f});
    CHECK(engine.Exchange(req) == TradeStatus::Ok);
    CHECK_EQ(engine.MoneyIn(f.Wallet), int64_t(150000));
    int stacks = 0;
    for (ContainerId c : f.Wallet) for (ItemId id : f.Store.ItemsIn(c)) { stacks += f.Store.GetItem(id)->DefId == kMoneyDef; CHECK(f.Store.GetItem(id)->Qty <= 100000); }
    CHECK_EQ(stacks, 2);
    // Further small grants top the partial stack up instead of creating new ones.
    ExchangeRequest more = req; more.TxId = "grant:2"; more.Gives = {{kMoneyDef, 1000, 100.f}};
    CHECK(engine.Exchange(more) == TradeStatus::Ok);
    stacks = 0;
    for (ContainerId c : f.Wallet) for (ItemId id : f.Store.ItemsIn(c)) stacks += f.Store.GetItem(id)->DefId == kMoneyDef;
    CHECK_EQ(stacks, 2);
}

TEST(Trade_RepairPaysByWearAndRestoresDurability)
{
    Fixture f; CHECK(f.Init(2000, 8, 8));
    TradeEngine engine(f.Store, f.Table, f.Memory);
    ItemId gun = 0;
    CHECK(f.Store.AutoPlace("W_MOSIN", 1, f.Backpack, true, &gun, 40.f) == Status::Ok);
    TraderProgress progress;
    const int64_t quote = engine.RepairQuote(gun);
    CHECK_EQ(quote, int64_t(1080));   // 6000 * 30% * 60% wear
    const TradeResult r = engine.Repair(1, "repair:1", f.Wallet, progress, gun);
    CHECK(r.Ok());
    CHECK_NEAR(f.Store.GetItem(gun)->Durability, 100.0, 1e-6);
    CHECK_EQ(engine.MoneyIn(f.Wallet), int64_t(2000 - 1080));
    CHECK(engine.Repair(1, "repair:2", f.Wallet, progress, gun).Status == TradeStatus::WorthNothing);

    ItemId junk = 0;
    CHECK(f.Store.AutoPlace("W_MOSIN", 1, f.Backpack, true, &junk, 10.f) == Status::Ok);
    CHECK(engine.Repair(1, "repair:3", f.Wallet, progress, junk).Status == TradeStatus::InsufficientFunds);   // 1620 needed, 920 left
    CHECK_NEAR(f.Store.GetItem(junk)->Durability, 10.0, 1e-6);
}

namespace
{
struct FailingLedger : ITxLedger
{
    TxOutcome Result = TxOutcome::Error;
    int Calls = 0;
    bool Seen(const std::string&) const override { return false; }
    TxOutcome Commit(const ChangeSet&, const std::string&, const std::string&, AccountId, const std::string&) override { ++Calls; return Result; }
};
}

TEST(AT027_Trade_LedgerFailureRollsEverythingBack)
{
    Fixture f; CHECK(f.Init(1000));
    FailingLedger ledger;
    TradeEngine engine(f.Store, f.Table, ledger);
    TraderStockState stock; stock.Entries.push_back(MakeBasicEntry("MED_BANDAGE"));
    TraderProgress progress;
    const ItemStore before = f.Store;
    CHECK(engine.Buy(1, "buy:fail", f.Wallet, stock, progress, "MED_BANDAGE", 3).Status == TradeStatus::LedgerError);
    CHECK_EQ(ledger.Calls, 1);
    CHECK(f.Store.Equals(before));
    CHECK_EQ(progress.Turnover, int64_t(0));
    ledger.Result = TxOutcome::Duplicate;
    CHECK(engine.Buy(1, "buy:dup", f.Wallet, stock, progress, "MED_BANDAGE", 3).Status == TradeStatus::Duplicate);
    CHECK(f.Store.Equals(before));
}

namespace
{
std::string TempDb()
{
    char path[] = "/tmp/scavar_trade_XXXXXX";
    const int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    return path;
}

// Applies the DB transaction, then reports an error as if the process died before the in-memory commit.
struct CrashAfterDb : ITxLedger
{
    DbLedger& Inner;
    explicit CrashAfterDb(DbLedger& inner) : Inner(inner) {}
    bool Seen(const std::string& id) const override { return Inner.Seen(id); }
    TxOutcome Commit(const ChangeSet& c, const std::string& t, const std::string& k, AccountId a, const std::string& d) override
    {
        Inner.Commit(c, t, k, a, d);
        return TxOutcome::Error;
    }
};

struct DbFixture
{
    Fixture F;
    Db Database;
    AccountId Account = 0;
    std::string Path = TempDb();

    bool Init()
    {
        std::string err;
        if (!F.Init(1000)) return false;
        if (!Database.Open(Path, &err) || !Database.EnsureAccount("tester", &Account, &err)) { std::printf("    db error: %s\n", err.c_str()); return false; }
        if (!Database.Apply(F.Store.DrainChanges(), &err)) { std::printf("    db apply error: %s\n", err.c_str()); return false; }
        return true;
    }
    ~DbFixture() { Database.Close(); std::remove(Path.c_str()); std::remove((Path + "-wal").c_str()); std::remove((Path + "-shm").c_str()); }

    int64_t Reload(ItemCatalog& catalog, ItemStore& fresh, int64_t* bandages)
    {
        std::string err;
        fresh.Clear();
        if (!Database.LoadInto(fresh, &err)) { std::printf("    reload error: %s\n", err.c_str()); return -1; }
        int64_t money = 0, b = 0;
        for (const auto& [id, item] : fresh.AllItems()) { if (item.DefId == kMoneyDef) money += item.Qty; if (item.DefId == "MED_BANDAGE") b += item.Qty; }
        (void)catalog;
        if (bandages) *bandages = b;
        return money;
    }
};
}

TEST(AT027_Trade_DbLedgerCommitsItemAndMoneyAtomically)
{
    DbFixture d; CHECK(d.Init());
    DbLedger ledger(d.Database);
    TradeEngine engine(d.F.Store, d.F.Table, ledger);
    TraderStockState stock; stock.Entries.push_back(MakeBasicEntry("MED_BANDAGE"));
    TraderProgress progress;
    CHECK(engine.Buy(d.Account, "buy:db1", d.F.Wallet, stock, progress, "MED_BANDAGE", 2).Ok());

    {   // leftovers of the temporary container must not break later persistence of the store's dirty set
        std::string applyErr;
        CHECK(d.Database.Apply(d.F.Store.DrainChanges(), &applyErr));
    }
    ItemStore fresh(&d.F.Catalog); int64_t bandages = 0;
    CHECK_EQ(d.Reload(d.F.Catalog, fresh, &bandages), int64_t(844));
    CHECK_EQ(bandages, int64_t(2));
    std::string why;
    CHECK(d.Database.CheckInvariants(&d.F.Catalog, &why));

    // AT-028 against the real ledger: the replay is detected before anything changes.
    const TradeResult again = engine.Buy(d.Account, "buy:db1", d.F.Wallet, stock, progress, "MED_BANDAGE", 2);
    CHECK(again.Status == TradeStatus::Duplicate);
    CHECK_EQ(d.Reload(d.F.Catalog, fresh, &bandages), int64_t(844));
    CHECK_EQ(bandages, int64_t(2));
}

TEST(AT027_Trade_CrashBeforeAndAfterDbCommitIsAllOrNothing)
{
    DbFixture d; CHECK(d.Init());
    ItemStore fresh(&d.F.Catalog); int64_t bandages = -1;

    {   // Crash BEFORE the database transaction: neither effect reaches the disk.
        FailingLedger crash;
        TradeEngine engine(d.F.Store, d.F.Table, crash);
        TraderStockState stock; stock.Entries.push_back(MakeBasicEntry("MED_BANDAGE"));
        TraderProgress progress;
        CHECK(engine.Buy(d.Account, "buy:pre", d.F.Wallet, stock, progress, "MED_BANDAGE", 2).Status == TradeStatus::LedgerError);
        CHECK_EQ(d.Reload(d.F.Catalog, fresh, &bandages), int64_t(1000));
        CHECK_EQ(bandages, int64_t(0));
    }
    {   // Crash AFTER the database transaction: the process memory is dropped; the disk holds BOTH effects.
        DbLedger real(d.Database);
        CrashAfterDb crash(real);
        TradeEngine engine(d.F.Store, d.F.Table, crash);
        TraderStockState stock; stock.Entries.push_back(MakeBasicEntry("MED_BANDAGE"));
        TraderProgress progress;
        CHECK(engine.Buy(d.Account, "buy:post", d.F.Wallet, stock, progress, "MED_BANDAGE", 2).Status == TradeStatus::LedgerError);
        CHECK_EQ(d.Reload(d.F.Catalog, fresh, &bandages), int64_t(844));
        CHECK_EQ(bandages, int64_t(2));

        // After the "restart", retrying the same request is recognised and does not pay twice.
        ItemStore restarted(&d.F.Catalog);
        std::string err;
        CHECK(d.Database.LoadInto(restarted, &err));
        TradeEngine afterRestart(restarted, d.F.Table, real);
        TraderStockState stock2; stock2.Entries.push_back(MakeBasicEntry("MED_BANDAGE"));
        TraderProgress progress2;
        CHECK(afterRestart.Buy(d.Account, "buy:post", d.F.Wallet, stock2, progress2, "MED_BANDAGE", 2).Status == TradeStatus::Duplicate);
        CHECK_EQ(afterRestart.MoneyIn(d.F.Wallet), int64_t(844));
        CHECK_EQ(afterRestart.QtyOf(d.F.Wallet, "MED_BANDAGE"), int64_t(2));
    }
}

TEST(Tasks_CatalogLoadsAndIsValid)
{
    GameData game; ItemTable table; TaskCatalog catalog; std::string err;
    CHECK(game.Load(SCAVAR_DATA_DIR, &err) && table.Load(SCAVAR_DATA_DIR, game, &err));
    CHECK(catalog.Load(SCAVAR_DATA_DIR, table, game, &err));
    if (!err.empty()) std::printf("    task load error: %s\n", err.c_str());
    int slice = 0, alpha = 0;
    for (const auto& t : catalog.Rows) { slice += t.Release_ == Release::Slice; alpha += t.Release_ == Release::Alpha; }
    CHECK_EQ(slice, 3);
    CHECK_EQ(alpha, 12);
    CHECK(catalog.Find("AL_ST_04") != nullptr);
    CHECK(catalog.Find("NOPE") == nullptr);

    GameData noPoi = game;
    noPoi.Pois.clear();
    TaskCatalog broken;
    CHECK(!broken.Load(SCAVAR_DATA_DIR, table, noPoi, &err));   // reach targets must exist in poi.csv
}

namespace
{
struct TaskFixture
{
    Fixture F;
    TaskCatalog Catalog;
    bool Init()
    {
        std::string err;
        if (!F.Init(0, 6, 8)) return false;
        if (!Catalog.Load(SCAVAR_DATA_DIR, F.Table, F.Game, &err)) { std::printf("    %s\n", err.c_str()); return false; }
        return true;
    }
};
}

TEST(AT030_Tasks_RewardIsGrantedExactlyOnce)
{
    TaskFixture t; CHECK(t.Init());
    TradeEngine engine(t.F.Store, t.F.Table, t.F.Memory);
    TaskBoard board(t.Catalog, 42);
    CHECK(t.F.Store.AutoPlace("TOOL_BATTERY", 3, t.F.Backpack, true) == Status::Ok);

    CHECK(board.Accept("AL_ST_01", 0) == TaskResult::Ok);
    CHECK(board.Accept("AL_ST_01", 0) == TaskResult::NotAvailable);
    CHECK(board.Complete("AL_ST_01", engine, t.F.Wallet, 10).Result == TaskResult::Ok);
    CHECK_EQ(engine.MoneyIn(t.F.Wallet), int64_t(300));
    CHECK_EQ(engine.QtyOf(t.F.Wallet, "TOOL_BATTERY"), int64_t(1));     // two taken
    CHECK_EQ(engine.QtyOf(t.F.Wallet, "A_918_FMJ"), int64_t(12));

    const TaskOutcome second = board.Complete("AL_ST_01", engine, t.F.Wallet, 11);
    CHECK(second.Result == TaskResult::AlreadyCompleted);
    CHECK_EQ(engine.MoneyIn(t.F.Wallet), int64_t(300));
    CHECK(board.Accept("AL_ST_01", 100000) == TaskResult::AlreadyCompleted);   // not repeatable
    CHECK_EQ(board.Trader("storekeeper").TasksCompleted, 1);
}

TEST(AT030_Tasks_RetryAfterCrashDoesNotPayAgain)
{
    TaskFixture t; CHECK(t.Init());
    TradeEngine engine(t.F.Store, t.F.Table, t.F.Memory);
    CHECK(t.F.Store.AutoPlace("TOOL_BATTERY", 2, t.F.Backpack, true) == Status::Ok);
    {
        TaskBoard board(t.Catalog, 7);
        CHECK(board.Accept("AL_ST_01", 0) == TaskResult::Ok);
        CHECK(board.Complete("AL_ST_01", engine, t.F.Wallet, 5).Ok());
    }
    // The board state was lost (crash before it was saved) but the ledger remembers the payment.
    TaskBoard recovered(t.Catalog, 7);
    CHECK(recovered.Accept("AL_ST_01", 0) == TaskResult::Ok);
    const TaskOutcome again = recovered.Complete("AL_ST_01", engine, t.F.Wallet, 6);
    CHECK(again.Ok());
    CHECK(!again.RewardGranted);
    CHECK_EQ(engine.MoneyIn(t.F.Wallet), int64_t(300));
    CHECK_EQ(engine.QtyOf(t.F.Wallet, "A_918_FMJ"), int64_t(12));
    CHECK(recovered.Progress("AL_ST_01").State == TaskState::Completed);
}

TEST(Tasks_ProgressTypesLoyaltyAndGating)
{
    TaskFixture t; CHECK(t.Init());
    TradeEngine engine(t.F.Store, t.F.Table, t.F.Memory);
    TaskBoard board(t.Catalog, 9);

    CHECK(board.Accept("AL_ST_04", 0) == TaskResult::LoyaltyTooLow);   // needs loyalty 2
    CHECK(board.Accept("NOPE", 0) == TaskResult::UnknownTask);
    CHECK(board.Complete("AL_ST_02", engine, t.F.Wallet, 0).Result == TaskResult::NotActive);

    CHECK(board.Accept("AL_ST_02", 0) == TaskResult::Ok);
    CHECK(board.Complete("AL_ST_02", engine, t.F.Wallet, 1).Result == TaskResult::NotReady);   // not there yet
    board.ReportReached("POI_999");                                                             // wrong place: nothing happens
    CHECK(board.Complete("AL_ST_02", engine, t.F.Wallet, 1).Result == TaskResult::NotReady);
    board.ReportReached("POI_024");
    CHECK(board.Complete("AL_ST_02", engine, t.F.Wallet, 2).Ok());
    CHECK_EQ(engine.MoneyIn(t.F.Wallet), int64_t(250));

    CHECK(board.Accept("AL_ST_03", 3) == TaskResult::Ok);
    board.ReportReached("POI_014");
    CHECK(board.Complete("AL_ST_03", engine, t.F.Wallet, 4).Ok());
    CHECK_EQ(engine.QtyOf(t.F.Wallet, "MED_BANDAGE"), int64_t(2));

    CHECK(t.F.Store.AutoPlace("TOOL_BATTERY", 2, t.F.Backpack, true) == Status::Ok);
    CHECK(board.Accept("AL_ST_01", 5) == TaskResult::Ok);
    CHECK(board.Complete("AL_ST_01", engine, t.F.Wallet, 6).Ok());
    CHECK_EQ(board.Trader("storekeeper").TasksCompleted, 3);
    CHECK_EQ(board.Trader("storekeeper").Loyalty, 2);                   // 3 tasks => level 2

    CHECK(board.Accept("AL_ST_04", 7) == TaskResult::Ok);
    board.ReportKills(2);
    CHECK(board.Complete("AL_ST_04", engine, t.F.Wallet, 8).Result == TaskResult::NotReady);
    board.ReportKills(5);                                               // progress is capped at the requirement
    CHECK_EQ(board.Progress("AL_ST_04").Count, 3);
    CHECK(board.Complete("AL_ST_04", engine, t.F.Wallet, 9).Ok());
    CHECK_EQ(engine.QtyOf(t.F.Wallet, "A_12_BUCK"), int64_t(8));

    CHECK(board.Accept("AL_MD_03", 10) == TaskResult::Ok);
    board.ReportHelp();
    CHECK(board.Complete("AL_MD_03", engine, t.F.Wallet, 11).Ok());
    std::string why;
    CHECK(t.F.Store.CheckInvariants(&why));
}

TEST(Tasks_FetchNeedsTheItemsAndDailyTasksRepeat)
{
    TaskFixture t; CHECK(t.Init());
    TradeEngine engine(t.F.Store, t.F.Table, t.F.Memory);
    TaskBoard board(t.Catalog, 5);
    CHECK(board.Accept("AL_MK_01", 0) == TaskResult::Ok);
    CHECK(board.Complete("AL_MK_01", engine, t.F.Wallet, 1).Result == TaskResult::NotReady);     // no stew yet
    CHECK(t.F.Store.AutoPlace("FOOD_CANNED_STEW", 1, t.F.Backpack, true) == Status::Ok);
    CHECK(board.Complete("AL_MK_01", engine, t.F.Wallet, 1).Result == TaskResult::NotReady);     // still short
    for (int i = 0; i < 2; ++i) CHECK(t.F.Store.AutoPlace("FOOD_CANNED_STEW", 1, t.F.Backpack, true) == Status::Ok);
    CHECK(board.Complete("AL_MK_01", engine, t.F.Wallet, 100).Ok());
    CHECK_EQ(engine.QtyOf(t.F.Wallet, "FOOD_CANNED_STEW"), int64_t(0));
    CHECK_EQ(engine.MoneyIn(t.F.Wallet), int64_t(350));

    CHECK(board.Accept("AL_MK_01", 100 + kSecondsPerDay - 1) == TaskResult::NotAvailable);       // daily cooldown
    CHECK(board.Accept("AL_MK_01", 100 + kSecondsPerDay) == TaskResult::Ok);
    for (int i = 0; i < 3; ++i) CHECK(t.F.Store.AutoPlace("FOOD_CANNED_STEW", 1, t.F.Backpack, true) == Status::Ok);
    CHECK(board.Complete("AL_MK_01", engine, t.F.Wallet, 100 + kSecondsPerDay + 5).Ok());
    CHECK_EQ(engine.MoneyIn(t.F.Wallet), int64_t(700));                                          // second completion has its own tx id
    CHECK_EQ(board.Progress("AL_MK_01").Completions, 2);
    CHECK(TaskBoard::TxIdFor(5, "AL_MK_01", 1) != TaskBoard::TxIdFor(5, "AL_MK_01", 2));
}
