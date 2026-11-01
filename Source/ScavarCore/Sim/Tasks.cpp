#include "Tasks.h"
#include "Csv.h"
#include <charconv>
#include <set>

namespace Scavar
{
namespace
{
bool ToInt(const std::string& s, int& out) { auto r = std::from_chars(s.data(), s.data() + s.size(), out); return r.ec == std::errc() && r.ptr == s.data() + s.size(); }
bool ParseType(const std::string& s, TaskType& out)
{
    if (s == "fetch") out = TaskType::Fetch; else if (s == "reach") out = TaskType::Reach;
    else if (s == "kill_marauders") out = TaskType::KillMarauders; else if (s == "help_resident") out = TaskType::HelpResident; else return false;
    return true;
}
bool ParseRelease(const std::string& s, Release& out)
{
    if (s == "slice") out = Release::Slice; else if (s == "alpha") out = Release::Alpha; else if (s == "ea") out = Release::EA; else return false;
    return true;
}
#define TASK_FAIL(msg) do { if (error) *error = (msg); return false; } while (0)
} // namespace

const char* TaskStateName(TaskState s)
{
    switch (s) { case TaskState::Available: return "available"; case TaskState::Active: return "active"; case TaskState::Completed: return "completed"; }
    return "?";
}

const TaskDef* TaskCatalog::Find(const std::string& id) const
{
    for (const auto& t : Rows) if (t.Id == id) return &t;
    return nullptr;
}

bool TaskCatalog::Load(const std::string& dir, const ItemTable& items, const GameData& game, std::string* error)
{
    Rows.clear();
    CsvTable t;
    std::string err;
    if (!LoadCsvFile(dir + "/tasks.csv", t, &err)) TASK_FAIL(err);
    static const std::set<std::string> givers{"market", "medic", "storekeeper", "mechanic", "prototype"};
    for (size_t r = 0; r < t.Rows.size(); ++r)
    {
        TaskDef d;
        d.Id = t.Cell(r, "task_id"); d.Name = t.Cell(r, "name_ru"); d.Giver = t.Cell(r, "giver");
        d.Target = t.Cell(r, "target"); d.RewardItem = t.Cell(r, "reward_item"); d.Description = t.Cell(r, "description_ru");
        int repeat = 0;
        if (!ParseType(t.Cell(r, "type"), d.Type) || !ToInt(t.Cell(r, "count"), d.Count) || !ToInt(t.Cell(r, "reward_rubles"), d.RewardRubles) ||
            !ToInt(t.Cell(r, "reward_item_qty"), d.RewardItemQty) || !ToInt(t.Cell(r, "repeatable"), repeat) ||
            !ToInt(t.Cell(r, "required_loyalty"), d.RequiredLoyalty) || !ParseRelease(t.Cell(r, "release"), d.Release_))
            TASK_FAIL("tasks.csv: bad value in row for " + d.Id);
        d.Repeatable = repeat != 0;
        if (d.Id.empty() || Find(d.Id)) TASK_FAIL("tasks.csv: empty or duplicate id " + d.Id);
        if (!givers.count(d.Giver)) TASK_FAIL("tasks.csv: unknown giver for " + d.Id);
        if (d.Count < 1 || d.RewardRubles < 0 || d.RewardItemQty < 0 || d.RequiredLoyalty < 1 || d.RequiredLoyalty > 3) TASK_FAIL("tasks.csv: bad numbers for " + d.Id);
        if (d.RewardRubles == 0 && (d.RewardItem.empty() || d.RewardItemQty == 0)) TASK_FAIL("tasks.csv: empty reward for " + d.Id);
        if (d.RewardItem.empty() != (d.RewardItemQty == 0)) TASK_FAIL("tasks.csv: reward_item and reward_item_qty must be set together for " + d.Id);
        if (!d.RewardItem.empty())
        {
            const ItemRow* row = items.Find(d.RewardItem);
            if (!row) TASK_FAIL("tasks.csv: unknown reward item " + d.RewardItem + " in " + d.Id);
            if (d.RewardItemQty > row->StackMax) TASK_FAIL("tasks.csv: reward quantity exceeds stack size in " + d.Id);
        }
        switch (d.Type)
        {
        case TaskType::Fetch: if (!items.Find(d.Target)) TASK_FAIL("tasks.csv: unknown fetch item " + d.Target + " in " + d.Id); break;
        case TaskType::Reach: if (!game.FindPoi(d.Target)) TASK_FAIL("tasks.csv: unknown POI " + d.Target + " in " + d.Id); break;
        case TaskType::KillMarauders: if (d.Target != "AI-03") TASK_FAIL("tasks.csv: kill target must be AI-03 in " + d.Id); break;
        case TaskType::HelpResident: if (d.Target != "AI-05") TASK_FAIL("tasks.csv: help target must be AI-05 in " + d.Id); break;
        }
        Rows.push_back(std::move(d));
    }
    return true;
}

TaskProgress TaskBoard::Progress(const std::string& taskId) const
{
    auto it = Tasks.find(taskId);
    return it == Tasks.end() ? TaskProgress{} : it->second;
}

TaskResult TaskBoard::Accept(const std::string& taskId, int64_t nowS)
{
    const TaskDef* def = Catalog.Find(taskId);
    if (!def) return TaskResult::UnknownTask;
    if (Trader(def->Giver).Loyalty < def->RequiredLoyalty) return TaskResult::LoyaltyTooLow;
    TaskProgress& p = Tasks[taskId];
    if (p.State == TaskState::Active) return TaskResult::NotAvailable;
    if (p.State == TaskState::Completed)
    {
        if (!def->Repeatable) return TaskResult::AlreadyCompleted;
        if (nowS - p.LastCompletedAtS < kSecondsPerDay) return TaskResult::NotAvailable;
    }
    p.State = TaskState::Active;
    p.Count = 0;
    return TaskResult::Ok;
}

void TaskBoard::ReportReached(const std::string& poiId)
{
    for (const auto& def : Catalog.Rows)
    {
        if (def.Type != TaskType::Reach || def.Target != poiId) continue;
        TaskProgress& p = Tasks[def.Id];
        if (p.State == TaskState::Active) p.Count = def.Count;
    }
}

void TaskBoard::ReportKills(int count)
{
    if (count < 1) return;
    for (const auto& def : Catalog.Rows)
    {
        if (def.Type != TaskType::KillMarauders) continue;
        TaskProgress& p = Tasks[def.Id];
        if (p.State == TaskState::Active) p.Count = std::min(def.Count, p.Count + count);
    }
}

void TaskBoard::ReportHelp()
{
    for (const auto& def : Catalog.Rows)
    {
        if (def.Type != TaskType::HelpResident) continue;
        TaskProgress& p = Tasks[def.Id];
        if (p.State == TaskState::Active) p.Count = std::min(def.Count, p.Count + 1);
    }
}

std::string TaskBoard::TxIdFor(AccountId account, const std::string& taskId, int completionIndex)
{
    return "task:" + std::to_string(account) + ":" + taskId + ":" + std::to_string(completionIndex);
}

TaskOutcome TaskBoard::Complete(const std::string& taskId, TradeEngine& engine, const std::vector<ContainerId>& wallet, int64_t nowS)
{
    TaskOutcome out;
    const TaskDef* def = Catalog.Find(taskId);
    if (!def) return out;
    auto it = Tasks.find(taskId);
    if (it == Tasks.end() || it->second.State == TaskState::Available) { out.Result = TaskResult::NotActive; return out; }
    TaskProgress& p = it->second;
    if (p.State == TaskState::Completed) { out.Result = TaskResult::AlreadyCompleted; return out; }

    const bool isFetch = def->Type == TaskType::Fetch;
    const std::string txId = TxIdFor(Account, taskId, p.Completions + 1);
    // A recovered replay (ledger already has this tx) must not require the fetched items again.
    const bool paidAlready = engine.WasRecorded(txId);
    if (!paidAlready)
    {
        if (isFetch ? engine.QtyOf(wallet, def->Target) < def->Count : p.Count < def->Count) { out.Result = TaskResult::NotReady; return out; }
    }

    TradeStatus status = TradeStatus::Duplicate;
    if (!paidAlready)
    {
        ExchangeRequest req;
        req.TxId = txId; req.Kind = "task"; req.Account = Account; req.Wallet = wallet;
        if (isFetch) req.Takes.push_back({def->Target, def->Count, InvalidId});
        int rubles = def->RewardRubles;
        while (rubles > 0)
        {
            const int chunk = std::min(rubles, 100000);
            req.Gives.push_back({kMoneyDef, chunk, 100.f});
            rubles -= chunk;
        }
        if (!def->RewardItem.empty()) req.Gives.push_back({def->RewardItem, def->RewardItemQty, 100.f});
        req.DeltaJson = "{\"task\":\"" + taskId + "\"}";
        status = engine.Exchange(req);
    }

    if (status != TradeStatus::Ok && status != TradeStatus::Duplicate)
    {
        out.Result = TaskResult::TradeFailed;
        out.Trade = status;
        return out;
    }
    out.Trade = status;
    out.RewardGranted = status == TradeStatus::Ok;
    out.Result = TaskResult::Ok;
    p.State = TaskState::Completed;
    ++p.Completions;
    p.LastCompletedAtS = nowS;
    RecordTaskCompleted(Trader(def->Giver), Tuning);
    return out;
}
#undef TASK_FAIL
} // namespace Scavar
