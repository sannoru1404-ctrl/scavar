#pragma once
// Adapter: TradeEngine ledger backed by SQLite. Items and the tx_id row are written in ONE transaction
// (Db::ApplyWithTx), so a crash leaves either both effects or neither.
#include "Db.h"
#include "Sim/Trade.h"

namespace Scavar
{
class DbLedger : public ITxLedger
{
public:
    explicit DbLedger(Db& db) : Database(db) {}

    bool Seen(const std::string& txId) const override
    {
        if (!IsSafeTxId(txId)) return false;
        int64_t count = 0;
        std::string err;
        // txId is restricted to [A-Za-z0-9:_.-] by IsSafeTxId, so embedding it in SQL is safe.
        return Database.ScalarInt("SELECT COUNT(*) FROM ledger WHERE tx_id='" + txId + "'", &count, &err) && count > 0;
    }

    TxOutcome Commit(const ChangeSet& changes, const std::string& txId, const std::string& kind, AccountId account, const std::string& deltaJson) override
    {
        std::string err;
        switch (Database.ApplyWithTx(changes, txId, kind, account, deltaJson, &err))
        {
        case Db::TxResult::Applied: return TxOutcome::Applied;
        case Db::TxResult::Duplicate: return TxOutcome::Duplicate;
        case Db::TxResult::Error: break;
        }
        LastError = err;
        return TxOutcome::Error;
    }

    const std::string& LastErrorText() const { return LastError; }

private:
    Db& Database;
    std::string LastError;
};
} // namespace Scavar
