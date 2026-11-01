#pragma once
// SYS-09: atomic, idempotent exchanges of items and rubles on top of ItemStore.
// Every Buy/Sell/Repair/task reward is one Exchange: remove, add, record in the ledger, or nothing at all (AT-027/028).
#include "Inventory.h"
#include "Items.h"
#include "Pricing.h"
#include <set>
#include <string>
#include <vector>

namespace Scavar
{
inline constexpr const char* kMoneyDef = "MONEY_RUBLE";

enum class TxOutcome : uint8_t { Applied, Duplicate, Error };

// The ledger commit and the item changes must reach durable storage together, so the engine hands the whole
// ChangeSet to the ledger. In-memory ledger: tests and the listen-server before persistence is attached.
// Database ledger: see Source/ScavarPersistence/DbLedger.h (one SQL transaction for items + tx_id).
class ITxLedger
{
public:
    virtual ~ITxLedger() = default;
    virtual bool Seen(const std::string& txId) const = 0;
    virtual TxOutcome Commit(const ChangeSet& changes, const std::string& txId, const std::string& kind, AccountId account, const std::string& deltaJson) = 0;
};

class MemoryLedger : public ITxLedger
{
public:
    bool Seen(const std::string& txId) const override { return Done.count(txId) > 0; }
    TxOutcome Commit(const ChangeSet&, const std::string& txId, const std::string&, AccountId, const std::string&) override
    {
        return Done.insert(txId).second ? TxOutcome::Applied : TxOutcome::Duplicate;
    }
    bool TryRecord(const std::string& txId) { return Done.insert(txId).second; }
    size_t Size() const { return Done.size(); }

private:
    std::set<std::string> Done;
};

enum class TradeStatus : uint8_t
{
    Ok = 0, Duplicate, InvalidRequest, UnknownItem, NotForSale, OutOfStock, InsufficientFunds, InsufficientItems,
    NoSpace, WorthNothing, StoreError, LedgerError
};
const char* TradeStatusName(TradeStatus s);

struct Take { std::string DefId; int Qty = 0; ItemId Specific = InvalidId; };   // Specific != 0: that exact stack
struct Give { std::string DefId; int Qty = 0; float Durability = 100.f; };
struct ExchangeRequest
{
    std::string TxId, Kind;
    AccountId Account = 0;
    std::vector<ContainerId> Wallet;                       // containers searched/filled, in priority order
    std::vector<Take> Takes;
    std::vector<Give> Gives;
    std::vector<std::pair<ItemId, float>> SetDurability;   // applied atomically with the rest
    std::string DeltaJson = "{}";
};

struct TradeResult
{
    TradeStatus Status = TradeStatus::InvalidRequest;
    int64_t Amount = 0;      // rubles paid (buy/repair) or received (sell/reward)
    bool Ok() const { return Status == TradeStatus::Ok; }
};

class TradeEngine
{
public:
    TradeEngine(ItemStore& store, const ItemTable& items, ITxLedger& ledger, const PricingTuning& tuning = {})
        : Store(store), Items(items), Ledger(ledger), Tuning(tuning) {}

    // Generic primitive. All-or-nothing; replays of a known TxId return Duplicate and change nothing.
    TradeStatus Exchange(const ExchangeRequest& request);

    TradeResult Buy(AccountId account, const std::string& txId, const std::vector<ContainerId>& wallet, TraderStockState& stock,
                    TraderProgress& progress, const std::string& defId, int qty);
    TradeResult Sell(AccountId account, const std::string& txId, const std::vector<ContainerId>& wallet, TraderProgress& progress,
                     ItemId item, int qty);
    // Pays RepairCost(basePrice, wear) and restores durability to 100.
    TradeResult Repair(AccountId account, const std::string& txId, const std::vector<ContainerId>& wallet, TraderProgress& progress, ItemId item);

    bool WasRecorded(const std::string& txId) const { return Ledger.Seen(txId); }
    int64_t MoneyIn(const std::vector<ContainerId>& wallet) const;
    int64_t QtyOf(const std::vector<ContainerId>& wallet, const std::string& defId) const;
    int64_t BuyUnitPrice(const std::string& defId, int loyalty) const;     // -1: unknown item
    int64_t SellUnitPrice(const std::string& defId, int loyalty) const;    // -1: unknown item
    int64_t RepairQuote(ItemId item) const;                                // -1: unknown item

private:
    bool WalletValid(const std::vector<ContainerId>& wallet) const;
    bool InWallet(const std::vector<ContainerId>& wallet, ContainerId c) const;
    ContainerId EnsureScratch();   // temporary container (inside the open store transaction) so stacks keep their ids
    Status ReduceStack(ItemId id, int by);
    bool TakeAny(const std::vector<ContainerId>& wallet, const std::string& defId, int qty);
    bool GiveItems(const std::vector<ContainerId>& wallet, const Give& give);

    ItemStore& Store;
    ContainerId Scratch = InvalidId;
    const ItemTable& Items;
    ITxLedger& Ledger;
    PricingTuning Tuning;
};

// Ids are embedded in SQL by the database ledger, so they are restricted to [A-Za-z0-9:_.-], 1..128 chars.
bool IsSafeTxId(const std::string& txId);
} // namespace Scavar
