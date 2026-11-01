#include "TestFramework.h"
#include "Pricing.h"

using namespace Scavar;

TEST(Pricing_RoundTripLosesAtLeastHalf_AT029)
{
    const PricingTuning t;
    int checks = 0;
    const int64_t bases[] = {1, 2, 3, 4, 5, 7, 9, 10, 11, 13, 17, 25, 33, 99, 100, 101, 999, 1000, 12345, 250000, 99999999};
    for (int64_t base : bases)
    {
        for (int buyLl = 1; buyLl <= 3; ++buyLl)
        {
            for (int sellLl = 1; sellLl <= 3; ++sellLl)
            {
                const int64_t buy = BuyPrice(base, buyLl, t);
                const int64_t sell = SellPrice(base, sellLl, t);
                CHECK(buy >= sell);
                CHECK(sell * 2 <= buy);           // loses >= 50% of what was paid
                CHECK(buy >= base);
                ++checks;
            }
        }
    }
    // Dense sweep including every tiny price.
    for (int64_t base = 1; base <= 5000; ++base)
        for (int a = 1; a <= 3; ++a)
            for (int b = 1; b <= 3; ++b)
            {
                if (SellPrice(base, b, t) * 2 > BuyPrice(base, a, t)) { CHECK(false); return; }
                ++checks;
            }
    CHECK(checks > 45000);
}

TEST(Pricing_ExactValuesAndRounding)
{
    const PricingTuning t;
    CHECK_EQ(BuyPrice(100, 1, t), int64_t(130));
    CHECK_EQ(BuyPrice(100, 2, t), int64_t(115));
    CHECK_EQ(BuyPrice(100, 3, t), int64_t(100));
    CHECK_EQ(SellPrice(100, 1, t), int64_t(35));
    CHECK_EQ(SellPrice(100, 2, t), int64_t(40));
    CHECK_EQ(SellPrice(100, 3, t), int64_t(45));
    CHECK_EQ(BuyPrice(10, 1, t), int64_t(13));        // 1.30 * 10 must not float-round up to 14
    CHECK_EQ(BuyPrice(1, 2, t), int64_t(2));           // rounds up
    CHECK_EQ(SellPrice(1, 3, t), int64_t(0));          // rounds down
    CHECK_EQ(BuyPrice(0, 1, t), int64_t(0));
    CHECK_EQ(BuyPrice(-5, 1, t), int64_t(0));
    CHECK_EQ(BuyPrice(100, 99, t), BuyPrice(100, 3, t));   // loyalty clamped
    CHECK_EQ(BuyPrice(100, -4, t), BuyPrice(100, 1, t));
    CHECK(BuyPrice(INT64_MAX, 1, t) > 0);              // clamped, no overflow
}

TEST(Pricing_LoyaltyThresholds)
{
    const PricingTuning t;
    CHECK_EQ(LoyaltyFor(0, 0, t), 1);
    CHECK_EQ(LoyaltyFor(2, 49999, t), 1);
    CHECK_EQ(LoyaltyFor(3, 0, t), 2);
    CHECK_EQ(LoyaltyFor(0, 50000, t), 2);
    CHECK_EQ(LoyaltyFor(7, 249999, t), 2);
    CHECK_EQ(LoyaltyFor(8, 0, t), 3);
    CHECK_EQ(LoyaltyFor(0, 250000, t), 3);

    TraderProgress p;
    for (int i = 0; i < 3; ++i) RecordTaskCompleted(p, t);
    CHECK_EQ(p.Loyalty, 2);
    RecordTurnover(p, 250000, t);
    CHECK_EQ(p.Loyalty, 3);
    RecordTurnover(p, -1000000, t);                    // negative turnover ignored
    CHECK_EQ(p.Turnover, int64_t(250000));
    CHECK_EQ(p.Loyalty, 3);
}

TEST(Pricing_LoyaltyNeverDecreases)
{
    PricingTuning t;
    TraderProgress p;
    p.Loyalty = 3;                                     // e.g. loaded from DB under older thresholds
    RecordTaskCompleted(p, t);
    CHECK_EQ(p.Loyalty, 3);
    t.TasksForLevel3 = 1000; t.TurnoverForLevel3 = 1000000000;
    RecordTurnover(p, 1, t);
    CHECK_EQ(p.Loyalty, 3);
}

TEST(Pricing_RepairCost)
{
    const PricingTuning t;
    CHECK_EQ(RepairCost(1000, 1.0, t), int64_t(300));
    CHECK_EQ(RepairCost(1000, 0.5, t), int64_t(150));
    CHECK_EQ(RepairCost(1000, 0.0, t), int64_t(0));
    CHECK_EQ(RepairCost(1000, 2.0, t), int64_t(300));  // clamped
    CHECK_EQ(RepairCost(1000, -1.0, t), int64_t(0));
    CHECK_EQ(RepairCost(1, 0.001, t), int64_t(1));     // rounds up, never free when worn
    CHECK_EQ(RepairCost(0, 1.0, t), int64_t(0));
}

TEST(Stock_BasicGoodsAreUnlimited)
{
    StockEntry e = MakeBasicEntry("FOOD_BREAD");
    for (int i = 0; i < 1000; ++i) CHECK(ConsumeStock(e, 5));
    CHECK(!ConsumeStock(e, 0));
    CHECK(!ConsumeStock(e, -1));
}

TEST(Stock_ScarceGoodsRunOutAndRestockOnGrid)
{
    const PricingTuning t;
    Rng rng(11);
    StockEntry e = MakeScarceEntry("W_AK74", 1000, rng, t);
    CHECK(e.Stock >= 1 && e.Stock <= 3);
    CHECK_EQ(e.NextRestockAtS, int64_t(1000 + 4 * 3600));
    while (e.Stock > 0) CHECK(ConsumeStock(e, 1));
    CHECK(!CanBuy(e, 1));
    CHECK(!ConsumeStock(e, 1));

    TraderStockState s;
    s.Entries.push_back(e);
    s.Entries.push_back(MakeBasicEntry("FOOD_BREAD"));
    CHECK_EQ(RestockDue(s, 1000 + 4 * 3600 - 1, rng, t), 0);          // one second early
    CHECK(!CanBuy(*s.Find("W_AK74"), 1));
    CHECK_EQ(RestockDue(s, 1000 + 4 * 3600, rng, t), 1);              // exactly on time
    const StockEntry* after = s.Find("W_AK74");
    CHECK(after && after->Stock >= 1 && after->Stock <= 3);
    CHECK_EQ(after->NextRestockAtS, int64_t(1000 + 2 * 4 * 3600));
    CHECK_EQ(RestockDue(s, 1000 + 4 * 3600 + 5, rng, t), 0);          // not twice in one interval
}

TEST(Stock_LongGapRestocksOnceAndStaysOnGrid)
{
    const PricingTuning t;
    Rng rng(5);
    TraderStockState s;
    s.Entries.push_back(MakeScarceEntry("G_RGD5", 0, rng, t));
    s.Entries[0].Stock = 0;
    const int64_t late = 10 * 4 * 3600 + 123;
    CHECK_EQ(RestockDue(s, late, rng, t), 1);
    CHECK(s.Entries[0].Stock >= 1);
    CHECK(s.Entries[0].NextRestockAtS > late);
    CHECK_EQ(s.Entries[0].NextRestockAtS % t.RestockIntervalS, int64_t(0));
}

TEST(Stock_RestockNeverReducesStock)
{
    const PricingTuning t;
    Rng rng(99);
    for (int i = 0; i < 200; ++i)
    {
        TraderStockState s;
        StockEntry e = MakeScarceEntry("X", 0, rng, t);
        e.Stock = 3;
        s.Entries.push_back(e);
        RestockDue(s, t.RestockIntervalS, rng, t);
        CHECK_EQ(s.Entries[0].Stock, 3);
    }
}

TEST(Stock_DeterministicForSameSeed)
{
    const PricingTuning t;
    auto run = [&](uint64_t seed) {
        Rng rng(seed);
        TraderStockState s;
        for (int i = 0; i < 20; ++i) s.Entries.push_back(MakeScarceEntry("D" + std::to_string(i), 0, rng, t));
        for (auto& e : s.Entries) e.Stock = 0;
        RestockDue(s, t.RestockIntervalS, rng, t);
        std::vector<int> stocks;
        for (const auto& e : s.Entries) stocks.push_back(e.Stock);
        return stocks;
    };
    CHECK(run(7) == run(7));
    CHECK(run(7) != run(8));
}

TEST(TxRegistry_ExecutesOncePerTxId)
{
    TxRegistry reg;
    int runs = 0;
    auto action = [&] { ++runs; return TxResult{true, 130, "bought"}; };
    bool replayed = true;
    TxResult a = reg.ExecuteOnce("tx-1", action, &replayed);
    CHECK(a.Success && a.Value == 130 && !replayed);
    TxResult b = reg.ExecuteOnce("tx-1", action, &replayed);
    CHECK(replayed && b.Value == 130 && b.Info == "bought");
    CHECK_EQ(runs, 1);
    reg.ExecuteOnce("tx-2", action, &replayed);
    CHECK(!replayed);
    CHECK_EQ(runs, 2);
    CHECK_EQ(reg.Size(), size_t(2));
    CHECK(!reg.Record("tx-1", TxResult{false, 0, "other"}));
    CHECK_EQ(reg.Find("tx-1")->Value, int64_t(130));
    CHECK(reg.Find("nope") == nullptr);
}

TEST(TxRegistry_FailedResultsAreAlsoRemembered)
{
    TxRegistry reg;
    int runs = 0;
    auto failing = [&] { ++runs; return TxResult{false, 0, "no money"}; };
    reg.ExecuteOnce("tx-f", failing);
    TxResult again = reg.ExecuteOnce("tx-f", failing);
    CHECK(!again.Success);
    CHECK_EQ(runs, 1);
}
