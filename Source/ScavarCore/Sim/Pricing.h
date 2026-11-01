#pragma once
// SYS-09 trader pricing, loyalty, limited stock and an in-memory idempotency registry.
// All money maths is integer (permille), so results are exact and identical on every platform.
#include "Rng.h"
#include "Types.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Scavar
{
struct PricingTuning
{
    // Index 0..2 = loyalty level 1..3.
    int BuyPermille[3] = {1300, 1150, 1000};
    int SellPermille[3] = {350, 400, 450};
    int TasksForLevel2 = 3;
    int TasksForLevel3 = 8;
    int64_t TurnoverForLevel2 = 50000;
    int64_t TurnoverForLevel3 = 250000;
    int RepairPermille = 300;                 // repair cost = 30% of base price x wear
    int64_t RestockIntervalS = 4 * 3600;      // real seconds
    int ScarceStockMin = 1;
    int ScarceStockMax = 3;
};

// Base prices above this are clamped so permille multiplication cannot overflow int64.
constexpr int64_t MaxBasePrice = 1000000000000LL;

int ClampLoyalty(int loyalty);
// Player buys from trader: rounded UP. Player sells to trader: rounded DOWN. So Buy >= Sell always.
int64_t BuyPrice(int64_t basePrice, int loyalty, const PricingTuning& tuning = {});
int64_t SellPrice(int64_t basePrice, int loyalty, const PricingTuning& tuning = {});
// wearFraction in [0,1] (1 = fully worn). Rounded up; zero wear costs nothing.
int64_t RepairCost(int64_t basePrice, double wearFraction, const PricingTuning& tuning = {});

struct TraderProgress
{
    int Loyalty = 1;
    int TasksCompleted = 0;
    int64_t Turnover = 0;
};
// Level reachable from the given totals (ignores history).
int LoyaltyFor(int tasksCompleted, int64_t turnover, const PricingTuning& tuning = {});
// Both update Loyalty with max(current, reachable): loyalty never decreases.
void RecordTaskCompleted(TraderProgress& progress, const PricingTuning& tuning = {});
void RecordTurnover(TraderProgress& progress, int64_t amount, const PricingTuning& tuning = {});

struct StockEntry
{
    std::string DefId;
    bool Scarce = false;          // false = basic goods, unlimited
    int Stock = 0;                // meaningful only when Scarce
    int64_t NextRestockAtS = 0;
};

struct TraderStockState
{
    std::vector<StockEntry> Entries;
    StockEntry* Find(const std::string& defId);
    const StockEntry* Find(const std::string& defId) const;
};

StockEntry MakeBasicEntry(const std::string& defId);
StockEntry MakeScarceEntry(const std::string& defId, int64_t nowS, Rng& rng, const PricingTuning& tuning = {});
bool CanBuy(const StockEntry& entry, int quantity);
// Unlimited goods always succeed (quantity > 0); scarce goods need enough stock.
bool ConsumeStock(StockEntry& entry, int quantity);
// Restocks every scarce entry whose time has come; returns how many were restocked.
// Stock becomes max(current, roll in [min,max]); next restock stays on the original grid.
int RestockDue(TraderStockState& state, int64_t nowS, Rng& rng, const PricingTuning& tuning = {});

struct TxResult
{
    bool Success = false;
    int64_t Value = 0;
    std::string Info;
};

// Replaying the same tx_id returns the recorded result and never runs the action again (AT-028 logic).
class TxRegistry
{
public:
    const TxResult* Find(const std::string& txId) const;
    // Returns false (and keeps the old record) when txId was already recorded.
    bool Record(const std::string& txId, const TxResult& result);
    // Runs `action` at most once per txId. `replayed` (optional) tells whether a stored result was returned.
    TxResult ExecuteOnce(const std::string& txId, const std::function<TxResult()>& action, bool* replayed = nullptr);
    size_t Size() const { return Results.size(); }

private:
    std::unordered_map<std::string, TxResult> Results;
};
} // namespace Scavar
