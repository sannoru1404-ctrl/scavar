#include "Trade.h"
#include <algorithm>

namespace Scavar
{
const char* TradeStatusName(TradeStatus s)
{
    switch (s)
    {
    case TradeStatus::Ok: return "Ok";
    case TradeStatus::Duplicate: return "Duplicate";
    case TradeStatus::InvalidRequest: return "InvalidRequest";
    case TradeStatus::UnknownItem: return "UnknownItem";
    case TradeStatus::NotForSale: return "NotForSale";
    case TradeStatus::OutOfStock: return "OutOfStock";
    case TradeStatus::InsufficientFunds: return "InsufficientFunds";
    case TradeStatus::InsufficientItems: return "InsufficientItems";
    case TradeStatus::NoSpace: return "NoSpace";
    case TradeStatus::WorthNothing: return "WorthNothing";
    case TradeStatus::StoreError: return "StoreError";
    case TradeStatus::LedgerError: return "LedgerError";
    }
    return "?";
}

bool IsSafeTxId(const std::string& txId)
{
    if (txId.empty() || txId.size() > 128) return false;
    for (char c : txId)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ':' || c == '_' || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

namespace
{
std::map<ItemId, ItemInstance> Snapshot(const ItemStore& store, const std::vector<ContainerId>& wallet)
{
    std::map<ItemId, ItemInstance> snap;
    for (ContainerId c : wallet)
        for (ItemId id : store.ItemsIn(c))
            if (const ItemInstance* it = store.GetItem(id)) snap.emplace(id, *it);
    return snap;
}

ChangeSet Diff(const std::map<ItemId, ItemInstance>& before, const std::map<ItemId, ItemInstance>& after)
{
    ChangeSet cs;
    for (const auto& [id, item] : after)
    {
        auto it = before.find(id);
        if (it == before.end() || !(it->second == item)) cs.Items.push_back({id, item});
    }
    for (const auto& [id, item] : before)
        if (after.find(id) == after.end()) cs.Items.push_back({id, std::nullopt});
    return cs;
}
} // namespace

bool TradeEngine::WalletValid(const std::vector<ContainerId>& wallet) const
{
    if (wallet.empty()) return false;
    for (ContainerId c : wallet) if (!Store.GetContainer(c)) return false;
    return true;
}

bool TradeEngine::InWallet(const std::vector<ContainerId>& wallet, ContainerId c) const
{
    return std::find(wallet.begin(), wallet.end(), c) != wallet.end();
}

ContainerId TradeEngine::EnsureScratch()
{
    if (Scratch != InvalidId) return Scratch;
    Container c;
    c.Kind = ContainerKind::Ground;
    c.W = 16; c.H = 16;
    ContainerId id = InvalidId;
    return Store.AddContainer(c, &id) == Status::Ok ? (Scratch = id) : InvalidId;
}

Status TradeEngine::ReduceStack(ItemId id, int by)
{
    const ItemInstance* it = Store.GetItem(id);
    if (!it) return Status::NoSuchItem;
    if (by < 1 || by > it->Qty) return Status::InvalidArgument;
    if (by == it->Qty) return Store.Remove(id);
    if (it->Aux != 0) return Status::InvalidArgument;   // payload-carrying items are never partially consumed
    const ContainerId scratch = EnsureScratch();
    if (scratch == InvalidId) return Status::NoSpace;
    ItemId taken = InvalidId;
    if (Status s = Store.Split(id, by, scratch, 0, 0, false, &taken); s != Status::Ok) return s;
    return Store.Remove(taken);
}

bool TradeEngine::TakeAny(const std::vector<ContainerId>& wallet, const std::string& defId, int qty)
{
    std::vector<std::pair<int, ItemId>> stacks;   // (qty, id): smallest first so partial stacks are used up
    for (ContainerId c : wallet)
        for (ItemId id : Store.ItemsIn(c))
            if (const ItemInstance* it = Store.GetItem(id); it && it->DefId == defId) stacks.emplace_back(it->Qty, id);
    std::sort(stacks.begin(), stacks.end());
    int64_t have = 0;
    for (const auto& s : stacks) have += s.first;
    if (have < qty) return false;
    int need = qty;
    for (const auto& [stackQty, id] : stacks)
    {
        if (need == 0) break;
        const int by = std::min(need, stackQty);
        if (ReduceStack(id, by) != Status::Ok) return false;
        need -= by;
    }
    return need == 0;
}

bool TradeEngine::GiveItems(const std::vector<ContainerId>& wallet, const Give& give)
{
    const ItemRow* row = Items.Find(give.DefId);
    if (!row) return false;
    int remaining = give.Qty;

    // Top up partial stacks first (keeps money tidy and uses no extra cells).
    if (row->StackMax > 1 && give.Durability >= 100.f)
    {
        for (ContainerId c : wallet)
        {
            for (ItemId id : std::vector<ItemId>(Store.ItemsIn(c)))
            {
                if (remaining == 0) break;
                const ItemInstance* it = Store.GetItem(id);
                if (!it || it->DefId != give.DefId || it->Qty >= row->StackMax || it->Aux != 0 || it->Durability < 100.f) continue;
                const int add = std::min(remaining, row->StackMax - it->Qty);
                const ContainerId scratch = EnsureScratch();
                if (scratch == InvalidId) return false;
                ItemId extra = InvalidId;
                if (Store.Create(give.DefId, add, scratch, 0, 0, false, &extra, give.Durability) != Status::Ok) return false;
                if (Store.Merge(extra, id, add) != Status::Ok) return false;
                remaining -= add;
            }
        }
    }

    while (remaining > 0)
    {
        const int chunk = std::min(remaining, row->StackMax);
        bool placed = false;
        for (ContainerId c : wallet)
        {
            if (Store.AutoPlace(give.DefId, chunk, c, true, nullptr, give.Durability) == Status::Ok) { placed = true; break; }
        }
        if (!placed) return false;
        remaining -= chunk;
    }
    return true;
}

TradeStatus TradeEngine::Exchange(const ExchangeRequest& req)
{
    if (!IsSafeTxId(req.TxId) || req.Kind.empty() || (req.Takes.empty() && req.Gives.empty() && req.SetDurability.empty())) return TradeStatus::InvalidRequest;
    if (!WalletValid(req.Wallet)) return TradeStatus::InvalidRequest;
    for (const Take& t : req.Takes)
    {
        if (t.Qty < 1) return TradeStatus::InvalidRequest;
        if (t.Specific == InvalidId && !Items.Find(t.DefId)) return TradeStatus::UnknownItem;
        if (t.Specific != InvalidId)
        {
            const ItemInstance* it = Store.GetItem(t.Specific);
            if (!it) return TradeStatus::InsufficientItems;
            if (!InWallet(req.Wallet, it->Container) || (!t.DefId.empty() && it->DefId != t.DefId) || it->Qty < t.Qty) return TradeStatus::InvalidRequest;
        }
    }
    for (const Give& g : req.Gives)
    {
        if (g.Qty < 1) return TradeStatus::InvalidRequest;
        if (!Items.Find(g.DefId)) return TradeStatus::UnknownItem;
    }
    for (const auto& d : req.SetDurability)
    {
        const ItemInstance* it = Store.GetItem(d.first);
        if (!it || !InWallet(req.Wallet, it->Container)) return TradeStatus::InvalidRequest;
    }
    if (Ledger.Seen(req.TxId)) return TradeStatus::Duplicate;

    const auto before = Snapshot(Store, req.Wallet);
    Store.BeginTx();
    Scratch = InvalidId;
    auto fail = [&](TradeStatus s) { Store.Rollback(); Scratch = InvalidId; return s; };

    for (const Take& t : req.Takes)
    {
        if (t.Specific != InvalidId)
        {
            if (ReduceStack(t.Specific, t.Qty) != Status::Ok) return fail(TradeStatus::StoreError);
        }
        else if (!TakeAny(req.Wallet, t.DefId, t.Qty))
        {
            return fail(TradeStatus::InsufficientItems);
        }
    }
    for (const auto& d : req.SetDurability)
        if (Store.SetDurability(d.first, d.second) != Status::Ok) return fail(TradeStatus::StoreError);
    for (const Give& g : req.Gives)
        if (!GiveItems(req.Wallet, g)) return fail(TradeStatus::NoSpace);

    if (Scratch != InvalidId)
    {
        const bool dropped = Store.RemoveContainer(Scratch) == Status::Ok;
        Scratch = InvalidId;
        if (!dropped) return fail(TradeStatus::StoreError);
    }
    const ChangeSet changes = Diff(before, Snapshot(Store, req.Wallet));
    switch (Ledger.Commit(changes, req.TxId, req.Kind, req.Account, req.DeltaJson))
    {
    case TxOutcome::Applied:
        return Store.Commit() == Status::Ok ? TradeStatus::Ok : TradeStatus::StoreError;
    case TxOutcome::Duplicate:
        return fail(TradeStatus::Duplicate);
    case TxOutcome::Error:
        break;
    }
    return fail(TradeStatus::LedgerError);
}

int64_t TradeEngine::MoneyIn(const std::vector<ContainerId>& wallet) const { return QtyOf(wallet, kMoneyDef); }

int64_t TradeEngine::QtyOf(const std::vector<ContainerId>& wallet, const std::string& defId) const
{
    int64_t total = 0;
    for (ContainerId c : wallet)
        for (ItemId id : Store.ItemsIn(c))
            if (const ItemInstance* it = Store.GetItem(id); it && it->DefId == defId) total += it->Qty;
    return total;
}

int64_t TradeEngine::BuyUnitPrice(const std::string& defId, int loyalty) const
{
    const ItemRow* row = Items.Find(defId);
    return row ? BuyPrice(row->BasePrice, loyalty, Tuning) : -1;
}

int64_t TradeEngine::SellUnitPrice(const std::string& defId, int loyalty) const
{
    const ItemRow* row = Items.Find(defId);
    return row ? SellPrice(row->BasePrice, loyalty, Tuning) : -1;
}

int64_t TradeEngine::RepairQuote(ItemId item) const
{
    const ItemInstance* it = Store.GetItem(item);
    const ItemRow* row = it ? Items.Find(it->DefId) : nullptr;
    if (!row) return -1;
    const double wear = std::clamp((100.0 - it->Durability) / 100.0, 0.0, 1.0);
    return RepairCost(row->BasePrice, wear, Tuning);
}

TradeResult TradeEngine::Buy(AccountId account, const std::string& txId, const std::vector<ContainerId>& wallet, TraderStockState& stock,
                             TraderProgress& progress, const std::string& defId, int qty)
{
    TradeResult res;
    if (qty < 1 || defId == kMoneyDef) { res.Status = TradeStatus::InvalidRequest; return res; }
    const ItemRow* row = Items.Find(defId);
    if (!row) { res.Status = TradeStatus::UnknownItem; return res; }
    StockEntry* entry = stock.Find(defId);
    if (!entry) { res.Status = TradeStatus::NotForSale; return res; }
    if (!CanBuy(*entry, qty)) { res.Status = TradeStatus::OutOfStock; return res; }
    const int64_t unit = BuyPrice(row->BasePrice, progress.Loyalty, Tuning);
    if (unit <= 0) { res.Status = TradeStatus::WorthNothing; return res; }
    const int64_t total = unit * qty;
    if (MoneyIn(wallet) < total) { res.Status = TradeStatus::InsufficientFunds; return res; }

    ExchangeRequest req;
    req.TxId = txId; req.Kind = "buy"; req.Account = account; req.Wallet = wallet;
    req.Takes.push_back({kMoneyDef, static_cast<int>(total), InvalidId});
    req.Gives.push_back({defId, qty, 100.f});
    req.DeltaJson = "{\"buy\":\"" + defId + "\",\"qty\":" + std::to_string(qty) + ",\"rub\":" + std::to_string(total) + "}";
    res.Status = Exchange(req);
    if (res.Ok())
    {
        ConsumeStock(*entry, qty);
        RecordTurnover(progress, total, Tuning);
        res.Amount = total;
    }
    return res;
}

TradeResult TradeEngine::Sell(AccountId account, const std::string& txId, const std::vector<ContainerId>& wallet, TraderProgress& progress,
                              ItemId item, int qty)
{
    TradeResult res;
    const ItemInstance* it = Store.GetItem(item);
    if (!it) { res.Status = TradeStatus::UnknownItem; return res; }
    if (qty < 1 || qty > it->Qty || it->DefId == kMoneyDef || !InWallet(wallet, it->Container)) { res.Status = TradeStatus::InvalidRequest; return res; }
    const ItemRow* row = Items.Find(it->DefId);
    if (!row) { res.Status = TradeStatus::UnknownItem; return res; }
    const int64_t unit = SellPrice(row->BasePrice, progress.Loyalty, Tuning);
    if (unit <= 0) { res.Status = TradeStatus::WorthNothing; return res; }
    const int64_t total = unit * qty;

    ExchangeRequest req;
    req.TxId = txId; req.Kind = "sell"; req.Account = account; req.Wallet = wallet;
    req.Takes.push_back({it->DefId, qty, item});
    req.Gives.push_back({kMoneyDef, static_cast<int>(total), 100.f});
    req.DeltaJson = "{\"sell\":\"" + it->DefId + "\",\"qty\":" + std::to_string(qty) + ",\"rub\":" + std::to_string(total) + "}";
    res.Status = Exchange(req);
    if (res.Ok()) { RecordTurnover(progress, total, Tuning); res.Amount = total; }
    return res;
}

TradeResult TradeEngine::Repair(AccountId account, const std::string& txId, const std::vector<ContainerId>& wallet, TraderProgress& progress, ItemId item)
{
    TradeResult res;
    const ItemInstance* it = Store.GetItem(item);
    if (!it) { res.Status = TradeStatus::UnknownItem; return res; }
    if (!InWallet(wallet, it->Container)) { res.Status = TradeStatus::InvalidRequest; return res; }
    const int64_t cost = RepairQuote(item);
    if (cost < 0) { res.Status = TradeStatus::UnknownItem; return res; }
    if (cost == 0 || it->Durability >= 100.f) { res.Status = TradeStatus::WorthNothing; return res; }
    if (MoneyIn(wallet) < cost) { res.Status = TradeStatus::InsufficientFunds; return res; }

    ExchangeRequest req;
    req.TxId = txId; req.Kind = "repair"; req.Account = account; req.Wallet = wallet;
    req.Takes.push_back({kMoneyDef, static_cast<int>(cost), InvalidId});
    req.SetDurability.emplace_back(item, 100.f);
    req.DeltaJson = "{\"repair\":\"" + it->DefId + "\",\"rub\":" + std::to_string(cost) + "}";
    res.Status = Exchange(req);
    if (res.Ok()) { RecordTurnover(progress, cost, Tuning); res.Amount = cost; }
    return res;
}
} // namespace Scavar
