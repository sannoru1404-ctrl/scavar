#pragma once
// SYS-09 rule 5: trader tasks. Rewards go through TradeEngine::Exchange with a deterministic tx id,
// so a reward is granted exactly once even if a completion request is retried or the server restarts.
#include "GameData.h"
#include "Items.h"
#include "Pricing.h"
#include "Trade.h"
#include <map>
#include <string>
#include <vector>

namespace Scavar
{
enum class TaskType : uint8_t { Fetch = 0, Reach, KillMarauders, HelpResident };

struct TaskDef
{
    std::string Id, Name, Giver, Target, Description, RewardItem;
    TaskType Type = TaskType::Fetch;
    int Count = 1;
    int RewardRubles = 0, RewardItemQty = 0;
    bool Repeatable = false;      // daily
    int RequiredLoyalty = 1;
    Release Release_ = Release::Slice;
};

struct TaskCatalog
{
    std::vector<TaskDef> Rows;
    const TaskDef* Find(const std::string& id) const;
    // Validates: unique ids, reward/fetch items exist in items.csv, reach targets exist in poi.csv, rewards non-empty.
    bool Load(const std::string& dataDir, const ItemTable& items, const GameData& game, std::string* error);
};

enum class TaskState : uint8_t { Available = 0, Active, Completed };
const char* TaskStateName(TaskState s);

struct TaskProgress
{
    TaskState State = TaskState::Available;
    int Count = 0;
    int Completions = 0;
    int64_t LastCompletedAtS = -1;
    bool operator==(const TaskProgress&) const = default;
};

enum class TaskResult : uint8_t { Ok = 0, UnknownTask, LoyaltyTooLow, NotAvailable, AlreadyCompleted, NotActive, NotReady, TradeFailed };

struct TaskOutcome
{
    TaskResult Result = TaskResult::UnknownTask;
    TradeStatus Trade = TradeStatus::Ok;
    bool RewardGranted = false;   // false on a recovered duplicate: the reward was already paid before
    bool Ok() const { return Result == TaskResult::Ok; }
};

constexpr int64_t kSecondsPerDay = 86400;

class TaskBoard
{
public:
    TaskBoard(const TaskCatalog& catalog, AccountId account, const PricingTuning& tuning = {})
        : Catalog(catalog), Account(account), Tuning(tuning) {}

    TraderProgress& Trader(const std::string& giver) { return Traders[giver]; }
    const std::map<std::string, TraderProgress>& AllTraders() const { return Traders; }
    TaskProgress Progress(const std::string& taskId) const;

    TaskResult Accept(const std::string& taskId, int64_t nowS);
    void ReportReached(const std::string& poiId);
    void ReportKills(int count);
    void ReportHelp();
    // Completes an active task: takes the fetch items, pays the reward, updates loyalty. Atomic via the engine's ledger.
    TaskOutcome Complete(const std::string& taskId, TradeEngine& engine, const std::vector<ContainerId>& wallet, int64_t nowS);

    static std::string TxIdFor(AccountId account, const std::string& taskId, int completionIndex);

    // Persistence rows (tasks table); trader progress is exposed through AllTraders().
    const std::map<std::string, TaskProgress>& AllProgress() const { return Tasks; }
    void Import(const std::string& taskId, const TaskProgress& p) { Tasks[taskId] = p; }

private:
    const TaskCatalog& Catalog;
    AccountId Account;
    PricingTuning Tuning;
    std::map<std::string, TaskProgress> Tasks;
    std::map<std::string, TraderProgress> Traders;
};
} // namespace Scavar
