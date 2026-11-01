#include "Pricing.h"
#include <algorithm>

namespace Scavar
{
int ClampLoyalty(int loyalty) { return Clamp(loyalty, 1, 3); }

namespace
{
int64_t ClampBase(int64_t base) { return Clamp<int64_t>(base, 0, MaxBasePrice); }
} // namespace

int64_t BuyPrice(int64_t basePrice, int loyalty, const PricingTuning& t)
{
    const int64_t base = ClampBase(basePrice);
    if (base == 0) return 0;
    return (base * t.BuyPermille[ClampLoyalty(loyalty) - 1] + 999) / 1000;
}

int64_t SellPrice(int64_t basePrice, int loyalty, const PricingTuning& t)
{
    const int64_t base = ClampBase(basePrice);
    return (base * t.SellPermille[ClampLoyalty(loyalty) - 1]) / 1000;
}

int64_t RepairCost(int64_t basePrice, double wearFraction, const PricingTuning& t)
{
    const int64_t base = ClampBase(basePrice);
    const double w = Clamp(wearFraction, 0.0, 1.0);
    const int64_t wearPermille = static_cast<int64_t>(std::llround(w * 1000.0));
    if (base == 0 || wearPermille == 0) return 0;
    return (base * t.RepairPermille * wearPermille + 999999) / 1000000;
}

int LoyaltyFor(int tasks, int64_t turnover, const PricingTuning& t)
{
    if (tasks >= t.TasksForLevel3 || turnover >= t.TurnoverForLevel3) return 3;
    if (tasks >= t.TasksForLevel2 || turnover >= t.TurnoverForLevel2) return 2;
    return 1;
}

void RecordTaskCompleted(TraderProgress& p, const PricingTuning& t)
{
    ++p.TasksCompleted;
    p.Loyalty = std::max(ClampLoyalty(p.Loyalty), LoyaltyFor(p.TasksCompleted, p.Turnover, t));
}

void RecordTurnover(TraderProgress& p, int64_t amount, const PricingTuning& t)
{
    if (amount > 0) p.Turnover += amount;
    p.Loyalty = std::max(ClampLoyalty(p.Loyalty), LoyaltyFor(p.TasksCompleted, p.Turnover, t));
}

StockEntry* TraderStockState::Find(const std::string& defId)
{
    for (auto& e : Entries) if (e.DefId == defId) return &e;
    return nullptr;
}
const StockEntry* TraderStockState::Find(const std::string& defId) const
{
    for (const auto& e : Entries) if (e.DefId == defId) return &e;
    return nullptr;
}

StockEntry MakeBasicEntry(const std::string& defId)
{
    StockEntry e;
    e.DefId = defId;
    return e;
}

StockEntry MakeScarceEntry(const std::string& defId, int64_t nowS, Rng& rng, const PricingTuning& t)
{
    StockEntry e;
    e.DefId = defId;
    e.Scarce = true;
    e.Stock = rng.RangeInt(t.ScarceStockMin, t.ScarceStockMax);
    e.NextRestockAtS = nowS + t.RestockIntervalS;
    return e;
}

bool CanBuy(const StockEntry& e, int quantity)
{
    if (quantity <= 0) return false;
    return !e.Scarce || e.Stock >= quantity;
}

bool ConsumeStock(StockEntry& e, int quantity)
{
    if (!CanBuy(e, quantity)) return false;
    if (e.Scarce) e.Stock -= quantity;
    return true;
}

int RestockDue(TraderStockState& state, int64_t nowS, Rng& rng, const PricingTuning& t)
{
    int restocked = 0;
    for (auto& e : state.Entries)
    {
        if (!e.Scarce || nowS < e.NextRestockAtS) continue;
        e.Stock = std::max(e.Stock, rng.RangeInt(t.ScarceStockMin, t.ScarceStockMax));
        const int64_t missed = (nowS - e.NextRestockAtS) / t.RestockIntervalS + 1;
        e.NextRestockAtS += missed * t.RestockIntervalS;
        ++restocked;
    }
    return restocked;
}

const TxResult* TxRegistry::Find(const std::string& txId) const
{
    auto it = Results.find(txId);
    return it == Results.end() ? nullptr : &it->second;
}

bool TxRegistry::Record(const std::string& txId, const TxResult& result)
{
    return Results.emplace(txId, result).second;
}

TxResult TxRegistry::ExecuteOnce(const std::string& txId, const std::function<TxResult()>& action, bool* replayed)
{
    if (const TxResult* old = Find(txId))
    {
        if (replayed) *replayed = true;
        return *old;
    }
    if (replayed) *replayed = false;
    TxResult r = action();
    Results.emplace(txId, r);
    return r;
}
} // namespace Scavar
