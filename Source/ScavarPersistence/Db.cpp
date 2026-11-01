#include "Db.h"
#include <sqlite3.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace Scavar
{
namespace
{
// Schema v1 = SQL sketch of docs/04_SYSTEMS_SPEC.md SYS-12 with concrete types and constraints.
const char* const kMigration1 = R"SQL(
CREATE TABLE accounts(
  id INTEGER PRIMARY KEY,
  platform_id TEXT NOT NULL UNIQUE,
  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')));
CREATE TABLE characters(
  id INTEGER PRIMARY KEY,
  account_id INTEGER NOT NULL REFERENCES accounts(id),
  alive INTEGER NOT NULL DEFAULT 1,
  pos_x REAL NOT NULL DEFAULT 0, pos_y REAL NOT NULL DEFAULT 0, pos_z REAL NOT NULL DEFAULT 0, yaw REAL NOT NULL DEFAULT 0,
  hp_json TEXT NOT NULL DEFAULT '{}', stats_json TEXT NOT NULL DEFAULT '{}',
  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')));
CREATE TABLE containers(
  id INTEGER PRIMARY KEY,
  kind TEXT NOT NULL,
  owner_id INTEGER NOT NULL DEFAULT 0,
  w INTEGER NOT NULL CHECK(w > 0),
  h INTEGER NOT NULL CHECK(h > 0));
CREATE TABLE items(
  id INTEGER PRIMARY KEY,
  def_id TEXT NOT NULL,
  qty INTEGER NOT NULL CHECK(qty > 0),
  durability REAL NOT NULL DEFAULT 0,
  container_id INTEGER NOT NULL REFERENCES containers(id),
  x INTEGER NOT NULL, y INTEGER NOT NULL,
  rot INTEGER NOT NULL DEFAULT 0,
  flags INTEGER NOT NULL DEFAULT 0,
  aux INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')));
CREATE INDEX idx_items_container ON items(container_id);
CREATE TABLE world_state(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE trader_state(
  trader_id TEXT NOT NULL, def_id TEXT NOT NULL, stock INTEGER NOT NULL, next_restock INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(trader_id, def_id));
CREATE TABLE account_progress(
  account_id INTEGER NOT NULL REFERENCES accounts(id), trader_id TEXT NOT NULL,
  loyalty INTEGER NOT NULL DEFAULT 1, turnover INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(account_id, trader_id));
CREATE TABLE tasks(
  account_id INTEGER NOT NULL REFERENCES accounts(id), task_id TEXT NOT NULL, state TEXT NOT NULL,
  progress INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
  PRIMARY KEY(account_id, task_id));
CREATE TABLE ledger(
  tx_id TEXT PRIMARY KEY,
  kind TEXT NOT NULL,
  account_id INTEGER NOT NULL DEFAULT 0,
  delta_json TEXT NOT NULL DEFAULT '{}',
  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')));
)SQL";

const char* const kMigrations[] = { kMigration1 };
static_assert(sizeof(kMigrations) / sizeof(kMigrations[0]) == Db::kCurrentSchemaVersion, "one migration per schema version");

struct Stmt
{
    sqlite3_stmt* S = nullptr;
    int LastRc = SQLITE_OK;
    Stmt(sqlite3* db, const char* sql) { LastRc = sqlite3_prepare_v2(db, sql, -1, &S, nullptr); }
    ~Stmt() { sqlite3_finalize(S); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    bool Ok() const { return S != nullptr; }
    void Int(int i, int64_t v) { sqlite3_bind_int64(S, i, v); }
    void Real(int i, double v) { sqlite3_bind_double(S, i, v); }
    void Text(int i, const std::string& v) { sqlite3_bind_text(S, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT); }
    bool Run() { LastRc = sqlite3_step(S); return LastRc == SQLITE_DONE; }
    void Reset() { sqlite3_reset(S); sqlite3_clear_bindings(S); }
};

struct TxGuard
{
    Db& D;
    bool Active = false;
    explicit TxGuard(Db& d) : D(d) {}
    bool Begin(std::string* err) { Active = D.Exec("BEGIN IMMEDIATE", err); return Active; }
    bool Commit(std::string* err) { if (!D.Exec("COMMIT", err)) return false; Active = false; return true; }
    ~TxGuard() { if (Active) D.Exec("ROLLBACK", nullptr); }
};
} // namespace

void Db::SetError(std::string* err, const char* context) const
{
    if (!err) return;
    *err = std::string(context) + ": " + (Handle ? sqlite3_errmsg(Handle) : "database not open");
}

bool Db::SetState(const std::string& key, const std::string& value, std::string* err)
{
    Stmt s(Handle, "INSERT INTO world_state(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    if (!s.Ok()) { SetError(err, "prepare SetState"); return false; }
    s.Text(1, key);
    s.Text(2, value);
    if (!s.Run()) { SetError(err, "SetState"); return false; }
    return true;
}

bool Db::GetState(const std::string& key, std::string* value, std::string* err)
{
    Stmt s(Handle, "SELECT value FROM world_state WHERE key=?1");
    if (!s.Ok()) { SetError(err, "prepare GetState"); return false; }
    s.Text(1, key);
    const int rc = sqlite3_step(s.S);
    if (rc == SQLITE_ROW)
    {
        if (value) *value = reinterpret_cast<const char*>(sqlite3_column_text(s.S, 0));
        return true;
    }
    if (rc != SQLITE_DONE) SetError(err, "GetState");
    return false;
}

bool Db::DeleteState(const std::string& key, std::string* err)
{
    Stmt s(Handle, "DELETE FROM world_state WHERE key=?1");
    if (!s.Ok()) { SetError(err, "prepare DeleteState"); return false; }
    s.Text(1, key);
    if (!s.Run()) { SetError(err, "DeleteState"); return false; }
    return true;
}

bool Db::ListState(const std::string& keyPrefix, std::map<std::string, std::string>* out, std::string* err)
{
    Stmt s(Handle, "SELECT key,value FROM world_state WHERE substr(key,1,length(?1))=?1 ORDER BY key");
    if (!s.Ok()) { SetError(err, "prepare ListState"); return false; }
    s.Text(1, keyPrefix);
    int rc;
    while ((rc = sqlite3_step(s.S)) == SQLITE_ROW)
        (*out)[reinterpret_cast<const char*>(sqlite3_column_text(s.S, 0))] = reinterpret_cast<const char*>(sqlite3_column_text(s.S, 1));
    if (rc != SQLITE_DONE) { SetError(err, "ListState"); return false; }
    return true;
}

bool Db::Exec(const std::string& sql, std::string* err)
{
    if (!Handle) { if (err) *err = "database not open"; return false; }
    char* msg = nullptr;
    if (sqlite3_exec(Handle, sql.c_str(), nullptr, nullptr, &msg) != SQLITE_OK)
    {
        if (err) *err = std::string("exec failed: ") + (msg ? msg : "?");
        sqlite3_free(msg);
        return false;
    }
    return true;
}

bool Db::ScalarInt(const std::string& sql, int64_t* out, std::string* err)
{
    if (!Handle) { if (err) *err = "database not open"; return false; }
    Stmt s(Handle, sql.c_str());
    if (!s.Ok()) { SetError(err, "prepare"); return false; }
    if (sqlite3_step(s.S) != SQLITE_ROW) { SetError(err, "step"); return false; }
    *out = sqlite3_column_int64(s.S, 0);
    return true;
}

bool Db::Open(const std::string& path, std::string* err, Options options)
{
    Close();
    if (sqlite3_open_v2(path.c_str(), &Handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        SetError(err, "open");
        Close();
        return false;
    }
    sqlite3_busy_timeout(Handle, 5000);
    if (!Exec("PRAGMA foreign_keys=ON", err) || !Exec("PRAGMA journal_mode=WAL", err) ||
        !Exec(options.SyncFull ? "PRAGMA synchronous=FULL" : "PRAGMA synchronous=NORMAL", err) || !Migrate(err))
    {
        Close();
        return false;
    }
    return true;
}

void Db::Close()
{
    if (Handle) { sqlite3_close(Handle); Handle = nullptr; }
}

int Db::SchemaVersion() const
{
    if (!Handle) return -1;
    Stmt s(Handle, "PRAGMA user_version");
    if (!s.Ok() || sqlite3_step(s.S) != SQLITE_ROW) return -1;
    return sqlite3_column_int(s.S, 0);
}

bool Db::Migrate(std::string* err)
{
    int version = SchemaVersion();
    if (version < 0) { if (err) *err = "cannot read schema version"; return false; }
    if (version > kCurrentSchemaVersion)
    {
        if (err) *err = "database schema version " + std::to_string(version) + " is newer than supported " + std::to_string(kCurrentSchemaVersion);
        return false;
    }
    if (version == 0)
    {
        int64_t tables = 0;
        if (!ScalarInt("SELECT COUNT(*) FROM sqlite_master WHERE type='table'", &tables, err)) return false;
        if (tables > 0) { if (err) *err = "database has tables but schema version 0; refusing to migrate"; return false; }
    }
    for (; version < kCurrentSchemaVersion; ++version)
    {
        TxGuard tx(*this);
        if (!tx.Begin(err)) return false;
        if (!Exec(kMigrations[version], err)) return false;
        if (!Exec("PRAGMA user_version=" + std::to_string(version + 1), err)) return false;
        if (!tx.Commit(err)) return false;
    }
    return true;
}

bool Db::ApplyInsideTx(const ChangeSet& cs, std::string* err)
{
    Stmt upCont(Handle, "INSERT INTO containers(id,kind,owner_id,w,h) VALUES(?1,?2,?3,?4,?5) "
                        "ON CONFLICT(id) DO UPDATE SET kind=excluded.kind, owner_id=excluded.owner_id, w=excluded.w, h=excluded.h");
    Stmt upItem(Handle, "INSERT INTO items(id,def_id,qty,durability,container_id,x,y,rot,flags,aux) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) "
                        "ON CONFLICT(id) DO UPDATE SET def_id=excluded.def_id, qty=excluded.qty, durability=excluded.durability, "
                        "container_id=excluded.container_id, x=excluded.x, y=excluded.y, rot=excluded.rot, flags=excluded.flags, aux=excluded.aux");
    Stmt delItem(Handle, "DELETE FROM items WHERE id=?1");
    Stmt delCont(Handle, "DELETE FROM containers WHERE id=?1");
    if (!upCont.Ok() || !upItem.Ok() || !delItem.Ok() || !delCont.Ok()) { SetError(err, "prepare"); return false; }

    for (const ContainerChange& c : cs.Containers)
    {
        if (!c.After) continue;
        upCont.Reset();
        upCont.Int(1, c.After->Id); upCont.Text(2, ContainerKindName(c.After->Kind)); upCont.Int(3, c.After->Owner);
        upCont.Int(4, c.After->W); upCont.Int(5, c.After->H);
        if (!upCont.Run()) { SetError(err, "upsert container"); return false; }
    }
    for (const ItemChange& c : cs.Items)
    {
        if (c.After)
        {
            const ItemInstance& i = *c.After;
            upItem.Reset();
            upItem.Int(1, i.Id); upItem.Text(2, i.DefId); upItem.Int(3, i.Qty); upItem.Real(4, i.Durability);
            upItem.Int(5, i.Container); upItem.Int(6, i.X); upItem.Int(7, i.Y); upItem.Int(8, i.Rot ? 1 : 0); upItem.Int(9, i.Flags); upItem.Int(10, i.Aux);
            if (!upItem.Run()) { SetError(err, "upsert item"); return false; }
        }
        else
        {
            delItem.Reset();
            delItem.Int(1, c.Id);
            if (!delItem.Run()) { SetError(err, "delete item"); return false; }
        }
    }
    for (const ContainerChange& c : cs.Containers)
    {
        if (c.After) continue;
        delCont.Reset();
        delCont.Int(1, c.Id);
        if (!delCont.Run()) { SetError(err, "delete container"); return false; }
    }
    return true;
}

bool Db::Apply(const ChangeSet& changes, std::string* err)
{
    if (!Handle) { if (err) *err = "database not open"; return false; }
    if (changes.Empty()) return true;
    TxGuard tx(*this);
    return tx.Begin(err) && ApplyInsideTx(changes, err) && tx.Commit(err);
}

Db::TxResult Db::ApplyWithTx(const ChangeSet& changes, const std::string& txId, const std::string& kind, AccountId account,
                             const std::string& deltaJson, std::string* err)
{
    if (!Handle) { if (err) *err = "database not open"; return TxResult::Error; }
    TxGuard tx(*this);
    if (!tx.Begin(err)) return TxResult::Error;
    {
        Stmt ins(Handle, "INSERT OR IGNORE INTO ledger(tx_id,kind,account_id,delta_json) VALUES(?1,?2,?3,?4)");
        if (!ins.Ok()) { SetError(err, "prepare ledger"); return TxResult::Error; }
        ins.Text(1, txId); ins.Text(2, kind); ins.Int(3, account); ins.Text(4, deltaJson);
        if (!ins.Run()) { SetError(err, "insert ledger"); return TxResult::Error; }
        if (sqlite3_changes(Handle) == 0) return TxResult::Duplicate;   // guard rolls back; nothing applied
    }
    if (!ApplyInsideTx(changes, err) || !tx.Commit(err)) return TxResult::Error;
    return TxResult::Applied;
}

bool Db::TryRecordTx(const std::string& txId, const std::string& kind, AccountId account, const std::string& deltaJson, std::string* err)
{
    if (err) err->clear();
    if (!Handle) { if (err) *err = "database not open"; return false; }
    Stmt ins(Handle, "INSERT OR IGNORE INTO ledger(tx_id,kind,account_id,delta_json) VALUES(?1,?2,?3,?4)");
    if (!ins.Ok()) { SetError(err, "prepare ledger"); return false; }
    ins.Text(1, txId); ins.Text(2, kind); ins.Int(3, account); ins.Text(4, deltaJson);
    if (!ins.Run()) { SetError(err, "insert ledger"); return false; }
    return sqlite3_changes(Handle) == 1;
}

bool Db::LoadInto(ItemStore& store, std::string* err)
{
    if (!Handle) { if (err) *err = "database not open"; return false; }
    store.Clear();
    {
        Stmt s(Handle, "SELECT id,kind,owner_id,w,h FROM containers ORDER BY id");
        if (!s.Ok()) { SetError(err, "prepare containers"); return false; }
        while (sqlite3_step(s.S) == SQLITE_ROW)
        {
            Container c;
            c.Id = sqlite3_column_int64(s.S, 0);
            if (!ParseContainerKind(reinterpret_cast<const char*>(sqlite3_column_text(s.S, 1)), &c.Kind)) { if (err) *err = "unknown container kind in db"; return false; }
            c.Owner = sqlite3_column_int64(s.S, 2);
            c.W = sqlite3_column_int(s.S, 3); c.H = sqlite3_column_int(s.S, 4);
            if (store.ImportContainer(c) != Status::Ok) { if (err) *err = "cannot import container " + std::to_string(c.Id); return false; }
        }
    }
    {
        Stmt s(Handle, "SELECT id,def_id,qty,durability,container_id,x,y,rot,flags,aux FROM items ORDER BY id");
        if (!s.Ok()) { SetError(err, "prepare items"); return false; }
        while (sqlite3_step(s.S) == SQLITE_ROW)
        {
            ItemInstance i;
            i.Id = sqlite3_column_int64(s.S, 0);
            i.DefId = reinterpret_cast<const char*>(sqlite3_column_text(s.S, 1));
            i.Qty = sqlite3_column_int(s.S, 2);
            i.Durability = static_cast<float>(sqlite3_column_double(s.S, 3));
            i.Container = sqlite3_column_int64(s.S, 4);
            i.X = sqlite3_column_int(s.S, 5); i.Y = sqlite3_column_int(s.S, 6);
            i.Rot = sqlite3_column_int(s.S, 7) != 0;
            i.Flags = static_cast<uint32_t>(sqlite3_column_int64(s.S, 8));
            i.Aux = sqlite3_column_int(s.S, 9);
            if (Status st = store.ImportItem(i); st != Status::Ok) { if (err) *err = std::string("cannot import item ") + std::to_string(i.Id) + ": " + StatusName(st); return false; }
        }
    }
    int64_t maxItem = 0, maxCont = 0;
    if (!ScalarInt("SELECT COALESCE(MAX(id),0) FROM items", &maxItem, err) || !ScalarInt("SELECT COALESCE(MAX(id),0) FROM containers", &maxCont, err)) return false;
    store.SetNextIds(maxItem + 1, maxCont + 1);
    return true;
}

bool Db::CheckInvariants(const ItemCatalog* catalog, std::string* why)
{
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    if (!Handle) return fail("database not open");
    {
        Stmt s(Handle, "PRAGMA integrity_check");
        if (!s.Ok() || sqlite3_step(s.S) != SQLITE_ROW) return fail("integrity_check failed to run");
        const std::string r = reinterpret_cast<const char*>(sqlite3_column_text(s.S, 0));
        if (r != "ok") return fail("integrity_check: " + r);
    }
    {
        Stmt s(Handle, "PRAGMA foreign_key_check");
        if (!s.Ok()) return fail("foreign_key_check failed to run");
        if (sqlite3_step(s.S) == SQLITE_ROW) return fail("foreign key violation in table " + std::string(reinterpret_cast<const char*>(sqlite3_column_text(s.S, 0))));
    }
    int64_t n = 0;
    std::string err;
    if (!ScalarInt("SELECT COUNT(*) FROM items i LEFT JOIN containers c ON c.id=i.container_id WHERE c.id IS NULL", &n, &err)) return fail(err);
    if (n != 0) return fail(std::to_string(n) + " orphan item(s)");
    if (!ScalarInt("SELECT COUNT(*) FROM items WHERE qty < 1", &n, &err)) return fail(err);
    if (n != 0) return fail(std::to_string(n) + " item(s) with qty < 1");
    if (!ScalarInt("SELECT COUNT(*) - COUNT(DISTINCT id) FROM items", &n, &err)) return fail(err);
    if (n != 0) return fail("duplicate item ids");
    if (catalog)
    {
        ItemStore store(catalog);
        if (!LoadInto(store, &err)) return fail(err);
        if (!store.CheckInvariants(&err)) return fail(err);
    }
    return true;
}

bool Db::TotalQtyByDef(std::map<std::string, int64_t>* out, std::string* err)
{
    if (!Handle) { if (err) *err = "database not open"; return false; }
    out->clear();
    Stmt s(Handle, "SELECT def_id, SUM(qty) FROM items GROUP BY def_id");
    if (!s.Ok()) { SetError(err, "prepare totals"); return false; }
    while (sqlite3_step(s.S) == SQLITE_ROW)
        (*out)[reinterpret_cast<const char*>(sqlite3_column_text(s.S, 0))] = sqlite3_column_int64(s.S, 1);
    return true;
}

bool Db::VerifyConservation(const std::map<std::string, int64_t>& expected, std::string* why)
{
    std::map<std::string, int64_t> actual;
    std::string err;
    if (!TotalQtyByDef(&actual, &err)) { if (why) *why = err; return false; }
    if (actual == expected) return true;
    if (why)
    {
        *why = "quantity mismatch:";
        for (const auto& [def, qty] : expected)
        {
            auto it = actual.find(def);
            const int64_t have = it == actual.end() ? 0 : it->second;
            if (have != qty) *why += " " + def + " expected " + std::to_string(qty) + " got " + std::to_string(have) + ";";
        }
        for (const auto& [def, qty] : actual)
            if (!expected.count(def)) *why += " unexpected " + def + "=" + std::to_string(qty) + ";";
    }
    return false;
}

bool Db::Backup(const std::string& destPath, std::string* err)
{
    std::string quoted;
    for (char c : destPath) { quoted.push_back(c); if (c == '\'') quoted.push_back('\''); }
    return Exec("VACUUM INTO '" + quoted + "'", err);
}

bool Db::BackupToDir(const std::string& dir, int64_t unixSeconds, int keep, std::string* outPath, std::string* err)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { if (err) *err = "cannot create " + dir + ": " + ec.message(); return false; }
    char name[64];
    std::snprintf(name, sizeof(name), "backup_%012lld.sqlite", static_cast<long long>(unixSeconds));
    const fs::path dest = fs::path(dir) / name;
    fs::remove(dest, ec);
    if (!Backup(dest.string(), err)) return false;
    if (outPath) *outPath = dest.string();

    std::vector<std::string> files;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string f = it->path().filename().string();
        if (f.rfind("backup_", 0) == 0 && f.size() > 7 && f.substr(f.size() - 7) == ".sqlite") files.push_back(f);
    }
    std::sort(files.begin(), files.end());
    while (keep >= 0 && static_cast<int>(files.size()) > keep)
    {
        fs::remove(fs::path(dir) / files.front(), ec);
        files.erase(files.begin());
    }
    return true;
}

bool Db::EnsureAccount(const std::string& platformId, AccountId* out, std::string* err)
{
    if (!Handle) { if (err) *err = "database not open"; return false; }
    {
        Stmt ins(Handle, "INSERT OR IGNORE INTO accounts(platform_id) VALUES(?1)");
        if (!ins.Ok()) { SetError(err, "prepare"); return false; }
        ins.Text(1, platformId);
        if (!ins.Run()) { SetError(err, "insert account"); return false; }
    }
    Stmt sel(Handle, "SELECT id FROM accounts WHERE platform_id=?1");
    if (!sel.Ok()) { SetError(err, "prepare"); return false; }
    sel.Text(1, platformId);
    if (sqlite3_step(sel.S) != SQLITE_ROW) { SetError(err, "select account"); return false; }
    *out = sqlite3_column_int64(sel.S, 0);
    return true;
}
} // namespace Scavar
