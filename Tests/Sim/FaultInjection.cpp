// AT-034: kill a writer process with SIGKILL at random moments and prove the database stays consistent:
// DB invariants hold, per-def quantities are conserved (closed system) and no item id is duplicated.
#include "Db.h"
#include "Inventory.h"
#include "Rng.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace Scavar;
namespace fs = std::filesystem;

static ItemCatalog MakeCatalog()
{
    ItemCatalog c;
    c.Register({"AMMO", 1, 1, 0.01f, 60});
    c.Register({"BANDAGE", 1, 1, 0.05f, 4});
    c.Register({"RIFLE", 2, 4, 3.5f, 1});
    c.Register({"MAG", 1, 2, 0.3f, 1});
    c.Register({"BREAD", 1, 1, 0.4f, 5});
    return c;
}

static bool Setup(const std::string& path, const ItemCatalog& cat, std::map<std::string, int64_t>* expected, uint64_t seed)
{
    for (const char* ext : {"", "-wal", "-shm"}) { std::error_code ec; fs::remove(path + ext, ec); }
    ItemStore s(&cat);
    Rng rng(seed);
    const int sizes[8][2] = {{10, 10}, {10, 10}, {8, 8}, {6, 8}, {5, 5}, {4, 4}, {4, 3}, {3, 3}};
    std::vector<ContainerId> boxes;
    for (int i = 0; i < 8; ++i)
    {
        ContainerId id = 0;
        s.AddContainer({0, i % 2 ? ContainerKind::Backpack : ContainerKind::Stash, sizes[i][0], sizes[i][1], i}, &id);
        boxes.push_back(id);
    }
    const char* defs[] = {"AMMO", "BANDAGE", "RIFLE", "MAG", "BREAD"};
    const int maxQty[] = {60, 4, 1, 1, 5};
    for (int i = 0; i < 120; ++i)
    {
        const int d = rng.RangeInt(0, 4);
        s.AutoPlace(defs[d], rng.RangeInt(1, maxQty[d]), boxes[rng.RangeInt(0, 7)], true);
    }
    Db db;
    std::string err;
    if (!db.Open(path, &err) || !db.Apply(s.DrainChanges(), &err)) { std::fprintf(stderr, "setup failed: %s\n", err.c_str()); return false; }
    *expected = s.TotalQtyByDef();
    return db.CheckInvariants(&cat, &err);
}

// Never returns normally: keeps committing random closed-system transfers until killed.
[[noreturn]] static void ChildLoop(const std::string& path, const ItemCatalog& cat, uint64_t seed)
{
    Db db;
    ItemStore s(&cat);
    std::string err;
    if (!db.Open(path, &err) || !db.LoadInto(s, &err)) { std::fprintf(stderr, "child open failed: %s\n", err.c_str()); _exit(2); }
    Rng rng(seed);
    std::vector<ContainerId> boxes;
    for (const auto& [id, c] : s.AllContainers()) boxes.push_back(id);

    auto randomItem = [&]() -> ItemId {
        auto it = s.AllItems().begin();
        std::advance(it, rng.RangeInt(0, static_cast<int>(s.AllItems().size()) - 1));
        return it->first;
    };

    for (uint64_t n = 0;; ++n)
    {
        s.BeginTx();
        bool changed = false;
        const int steps = rng.RangeInt(1, 3);                              // multi-row transactions on purpose
        for (int k = 0; k < steps; ++k)
        {
            const ItemId id = randomItem();
            switch (rng.RangeInt(0, 3))
            {
            case 0: {
                ContainerId dst = boxes[rng.RangeInt(0, static_cast<int>(boxes.size()) - 1)];
                const Container* c = s.GetContainer(dst);
                changed |= s.Move(id, dst, rng.RangeInt(0, c->W - 1), rng.RangeInt(0, c->H - 1), rng.Chance(0.3)) == Status::Ok;
                break; }
            case 1:
                changed |= s.Merge(id, randomItem(), rng.Chance(0.5) ? 0 : rng.RangeInt(1, 10)) == Status::Ok;
                break;
            case 2: {
                ContainerId dst = boxes[rng.RangeInt(0, static_cast<int>(boxes.size()) - 1)];
                const Container* c = s.GetContainer(dst);
                changed |= s.Split(id, rng.RangeInt(1, 30), dst, rng.RangeInt(0, c->W - 1), rng.RangeInt(0, c->H - 1), rng.Chance(0.3)) == Status::Ok;
                break; }
            default:
                changed |= s.AutoMove(id, boxes[rng.RangeInt(0, static_cast<int>(boxes.size()) - 1)], true) == Status::Ok;
                break;
            }
        }
        if (!changed || rng.Chance(0.1)) { s.Rollback(); s.DrainChanges(); continue; }
        s.Commit();
        const std::string txId = "op-" + std::to_string(seed) + "-" + std::to_string(n);
        if (db.ApplyWithTx(s.DrainChanges(), txId, "fault", 0, "{}", &err) != Db::TxResult::Applied)
        {
            std::fprintf(stderr, "child apply failed: %s\n", err.c_str());
            _exit(3);
        }
    }
}

int main(int argc, char** argv)
{
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 200;
    std::error_code ec;
    const std::string path = argc > 2 ? argv[2] : (fs::temp_directory_path(ec) / ("scavar_fault_" + std::to_string(::getpid()) + ".sqlite")).string();
    const uint64_t baseSeed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1337;

    ItemCatalog cat = MakeCatalog();
    std::map<std::string, int64_t> expected;
    if (!Setup(path, cat, &expected, baseSeed)) return 1;

    Rng delays(baseSeed ^ 0xDEADBEEF);
    int64_t lastLedger = 0, totalOps = 0, zeroOpRounds = 0;
    for (int it = 0; it < iterations; ++it)
    {
        const pid_t pid = fork();
        if (pid < 0) { std::perror("fork"); return 1; }
        if (pid == 0) ChildLoop(path, cat, baseSeed * 1000003ULL + static_cast<uint64_t>(it));

        usleep(static_cast<useconds_t>(delays.RangeInt(15000, 160000)));
        kill(pid, SIGKILL);
        int status = 0;
        waitpid(pid, &status, 0);
        if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL))
        {
            std::fprintf(stderr, "iteration %d: child ended on its own (status %d) - writer failure\n", it, status);
            return 1;
        }

        Db db;
        std::string err;
        if (!db.Open(path, &err)) { std::fprintf(stderr, "iteration %d: reopen after kill failed: %s\n", it, err.c_str()); return 1; }
        if (!db.CheckInvariants(&cat, &err)) { std::fprintf(stderr, "iteration %d: invariants violated: %s\n", it, err.c_str()); return 1; }
        if (!db.VerifyConservation(expected, &err)) { std::fprintf(stderr, "iteration %d: %s\n", it, err.c_str()); return 1; }
        int64_t ledger = 0, dup = 0;
        if (!db.ScalarInt("SELECT COUNT(*) FROM ledger", &ledger, &err) || !db.ScalarInt("SELECT COUNT(*) - COUNT(DISTINCT id) FROM items", &dup, &err)) { std::fprintf(stderr, "query failed: %s\n", err.c_str()); return 1; }
        if (dup != 0) { std::fprintf(stderr, "iteration %d: duplicate item ids\n", it); return 1; }
        if (ledger < lastLedger) { std::fprintf(stderr, "iteration %d: committed transactions disappeared (%lld < %lld)\n", it, (long long)ledger, (long long)lastLedger); return 1; }
        totalOps += ledger - lastLedger;
        if (ledger == lastLedger) ++zeroOpRounds;
        lastLedger = ledger;
    }
    std::printf("AT-034 fault injection: %d SIGKILLs, %lld committed transactions survived, %lld rounds with no commit, invariants + conservation OK\n",
                iterations, (long long)totalOps, (long long)zeroOpRounds);
    if (totalOps < iterations) { std::fprintf(stderr, "workload too small to be meaningful (%lld ops)\n", (long long)totalOps); return 1; }
    for (const char* ext : {"", "-wal", "-shm"}) { std::error_code e2; fs::remove(path + ext, e2); }
    return 0;
}
