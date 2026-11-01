#pragma once
// SQLite persistence (SYS-12): WAL, foreign keys, schema v1, migrations, atomic change application, ledger idempotency.
// Nothing here throws; every failing call returns false / an error enum and fills `err`.
#include "Sim/Inventory.h"
#include <map>
#include <string>

struct sqlite3;

namespace Scavar
{
struct DbOptions
{
    bool SyncFull = false;   // false: WAL + synchronous=NORMAL (safe against process crash); true: also power loss
};

class Db
{
public:
    static constexpr int kCurrentSchemaVersion = 1;

    using Options = DbOptions;

    enum class TxResult { Applied, Duplicate, Error };

    Db() = default;
    ~Db() { Close(); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    // Opens (creating if needed) and migrates. Refuses databases whose schema is newer than kCurrentSchemaVersion.
    bool Open(const std::string& path, std::string* err, Options options = {});
    void Close();
    bool IsOpen() const { return Handle != nullptr; }
    int SchemaVersion() const;
    sqlite3* RawHandle() const { return Handle; }

    // Applies a drained change set in ONE `BEGIN IMMEDIATE` transaction (containers upserted, items upserted/deleted, containers deleted).
    bool Apply(const ChangeSet& changes, std::string* err);

    // Records `txId` in the ledger and applies `changes` in the SAME transaction. A repeated txId applies nothing (Duplicate).
    TxResult ApplyWithTx(const ChangeSet& changes, const std::string& txId, const std::string& kind, AccountId account,
                         const std::string& deltaJson, std::string* err);

    // Ledger-only helper: true if newly recorded, false if the tx id already existed (or on error: *err non-empty).
    bool TryRecordTx(const std::string& txId, const std::string& kind, AccountId account, const std::string& deltaJson, std::string* err = nullptr);

    // Replaces the contents of `store` with the database state and fixes up its id counters.
    bool LoadInto(ItemStore& store, std::string* err);

    // SQL-level checks (integrity, foreign keys, orphans, qty); with a catalog also loads the state and runs ItemStore::CheckInvariants.
    bool CheckInvariants(const ItemCatalog* catalog, std::string* why);
    bool TotalQtyByDef(std::map<std::string, int64_t>* out, std::string* err);
    bool VerifyConservation(const std::map<std::string, int64_t>& expected, std::string* why);

    // Backups via VACUUM INTO. BackupToDir names files backup_<12-digit unix seconds>.sqlite and keeps the newest `keep`.
    bool Backup(const std::string& destPath, std::string* err);
    bool BackupToDir(const std::string& dir, int64_t unixSeconds, int keep, std::string* outPath, std::string* err);

    // Small helpers (also used by tests).
    bool Exec(const std::string& sql, std::string* err);
    bool ScalarInt(const std::string& sql, int64_t* out, std::string* err);
    bool EnsureAccount(const std::string& platformId, AccountId* out, std::string* err);

private:
    bool Migrate(std::string* err);
    bool ApplyInsideTx(const ChangeSet& changes, std::string* err);
    void SetError(std::string* err, const char* context) const;

    sqlite3* Handle = nullptr;
};
} // namespace Scavar
