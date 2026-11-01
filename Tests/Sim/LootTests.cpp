#include "TestFramework.h"
#include "LootEconomy.h"

using namespace Scavar;

namespace
{
std::vector<LootSpawnPoint> MakeGrid(int buildings, int pointsPerBuilding, RiskTier tier, double spacing = 300.0, const char* tag = "residential")
{
    std::vector<LootSpawnPoint> pts;
    int64_t id = 1;
    for (int b = 0; b < buildings; ++b)
        for (int k = 0; k < pointsPerBuilding; ++k)
        {
            LootSpawnPoint p;
            p.Id = id++;
            p.Pos = {b * spacing, k * 2.0, 0};
            p.UsageTags = {tag};
            p.Tier = tier;
            p.BuildingId = 100 + b;
            pts.push_back(p);
        }
    return pts;
}

LootTypeDef MakeType(const char* id, int nominal, int min, Rarity r = Rarity::Common, bool weapon = false)
{
    LootTypeDef t;
    t.DefId = id; t.Nominal = nominal; t.Min = min; t.Rarity_ = r; t.IsWeapon = weapon;
    t.UsageTags = {"residential"};
    return t;
}

int CountOf(const LootWorldSnapshot& w, const char* id)
{
    auto it = w.Counts.find(id);
    return it == w.Counts.end() ? 0 : it->second;
}
} // namespace

TEST(Loot_RestocksToNominalAndRespectsBounds_AT009)
{
    const LootTuning tuning;
    std::vector<LootTypeDef> types = {MakeType("MED_BANDAGE", 20, 10), MakeType("FOOD_BREAD", 30, 15), MakeType("TOOL_KNIFE", 8, 4, Rarity::Uncommon)};
    auto points = MakeGrid(40, 5, RiskTier::R2);
    Rng rng(1);
    LootWorldSnapshot w;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        w.NowS = cycle * tuning.CycleIntervalS;
        auto d = RunCycle(types, points, w, tuning, rng);
        ApplyDecisions(d, w);
    }
    for (const auto& t : types)
    {
        CHECK(CountOf(w, t.DefId.c_str()) >= t.Min);
        CHECK(CountOf(w, t.DefId.c_str()) <= t.Nominal);
    }
    CHECK_EQ(CountOf(w, "MED_BANDAGE"), 20);
    CHECK_EQ(CountOf(w, "FOOD_BREAD"), 30);
}

TEST(Loot_NeverExceedsNominalEvenWithManyPoints_AT009)
{
    const LootTuning tuning;
    std::vector<LootTypeDef> types = {MakeType("A", 5, 5), MakeType("B", 3, 1)};
    auto points = MakeGrid(500, 8, RiskTier::R2);
    Rng rng(2);
    LootWorldSnapshot w;
    for (int cycle = 0; cycle < 30; ++cycle)
    {
        w.NowS = cycle * 3000.0;                    // cooldown always expired
        auto d = RunCycle(types, points, w, tuning, rng);
        ApplyDecisions(d, w);
        CHECK(CountOf(w, "A") <= 5);
        CHECK(CountOf(w, "B") <= 3);
        w.OccupiedPointIds.clear();
    }
    CHECK_EQ(CountOf(w, "A"), 5);
    CHECK_EQ(CountOf(w, "B"), 3);
}

TEST(Loot_NoSpawnWhenAtOrAboveMin)
{
    const LootTuning tuning;
    std::vector<LootTypeDef> types = {MakeType("A", 20, 10)};
    auto points = MakeGrid(20, 5, RiskTier::R2);
    Rng rng(3);
    LootWorldSnapshot w;
    w.Counts["A"] = 10;                              // == min: no restock
    CHECK(RunCycle(types, points, w, tuning, rng).empty());
    w.Counts["A"] = 25;                              // above nominal
    CHECK(RunCycle(types, points, w, tuning, rng).empty());
    w.Counts["A"] = 9;                               // below min: fills up to nominal only
    auto d = RunCycle(types, points, w, tuning, rng);
    CHECK_EQ(d.size(), size_t(11));
}

TEST(Loot_NoSpawnNearOrInViewOfPlayers_AT010)
{
    const LootTuning tuning;
    std::vector<LootTypeDef> types = {MakeType("A", 10000, 10000)};
    auto points = MakeGrid(60, 4, RiskTier::R2, 40.0);   // dense: many points within 100 m of the player
    LootWorldSnapshot w;
    w.PlayerPositions = {{600.0, 0.0, 0.0}};
    w.IsVisibleToAnyPlayer = [](const Vec3& p) { return p.x < 100.0; };   // west corner is "in view"
    Rng rng(4);
    auto d = RunCycle(types, points, w, tuning, rng);
    CHECK(!d.empty());
    std::unordered_map<int64_t, Vec3> pos;
    for (const auto& p : points) pos[p.Id] = p.Pos;
    for (const auto& s : d)
    {
        const Vec3 p = pos[s.PointId];
        CHECK((p - w.PlayerPositions[0]).Length() >= 100.0);
        CHECK(!(p.x < 100.0));
    }
    // Exactly the right points are excluded.
    size_t expected = 0;
    for (const auto& p : points)
        if ((p.Pos - w.PlayerPositions[0]).Length() >= 100.0 && !(p.Pos.x < 100.0)) ++expected;
    CHECK_EQ(d.size(), expected);
}

TEST(Loot_PlayerDistanceIsExact)
{
    const LootTuning tuning;
    std::vector<LootTypeDef> types = {MakeType("A", 10, 10)};
    LootSpawnPoint at99, at100;
    at99.Id = 1; at99.Pos = {99.9, 0, 0}; at99.UsageTags = {"residential"}; at99.BuildingId = 1;
    at100.Id = 2; at100.Pos = {100.0, 0, 0}; at100.UsageTags = {"residential"}; at100.BuildingId = 2;
    LootWorldSnapshot w;
    w.PlayerPositions = {{0, 0, 0}};
    Rng rng(5);
    auto d = RunCycle(types, {at99, at100}, w, tuning, rng);
    CHECK_EQ(d.size(), size_t(1));
    CHECK_EQ(d[0].PointId, int64_t(2));
}

TEST(Loot_GroundItemsExpireButContainedItemsNever_AT011)
{
    LootTypeDef shortLived = MakeType("A", 1, 1); shortLived.LifetimeS = 600;
    LootTypeDef longLived = MakeType("B", 1, 1);  longLived.LifetimeS = 7200;
    std::vector<GroundItemRecord> items = {
        {1, "A", 0, true}, {2, "A", 0, false}, {3, "B", 0, true}, {4, "UNKNOWN", 0, true}, {5, "A", 500, true}};
    auto expired = ExpiredGroundItems(items, {shortLived, longLived}, 700.0);
    CHECK(expired == std::vector<int64_t>({1}));                  // 2 in container, 3 young, 4 default 3600, 5 only 200 s old
    expired = ExpiredGroundItems(items, {shortLived, longLived}, 3600.0);
    CHECK(expired == std::vector<int64_t>({1, 4, 5}));
    expired = ExpiredGroundItems(items, {shortLived, longLived}, 1e9);
    CHECK(expired == std::vector<int64_t>({1, 3, 4, 5}));         // never item 2
}

TEST(Loot_BuildingCooldown)
{
    const LootTuning tuning;
    std::vector<LootTypeDef> types = {MakeType("A", 1000, 1000)};
    auto points = MakeGrid(10, 4, RiskTier::R2);
    Rng rng(6);
    LootWorldSnapshot w;
    w.NowS = 0;
    auto first = RunCycle(types, points, w, tuning, rng);
    CHECK_EQ(first.size(), points.size());
    ApplyDecisions(first, w);
    w.OccupiedPointIds.clear();                       // pretend everything was picked up
    w.NowS = 300;
    CHECK(RunCycle(types, points, w, tuning, rng).empty());
    w.NowS = 45 * 60 - 1;
    CHECK(RunCycle(types, points, w, tuning, rng).empty());
    w.NowS = 45 * 60;
    CHECK_EQ(RunCycle(types, points, w, tuning, rng).size(), points.size());
}

TEST(Loot_OccupiedPointsAreSkipped)
{
    const LootTuning tuning;
    std::vector<LootTypeDef> types = {MakeType("A", 100, 100)};
    auto points = MakeGrid(2, 5, RiskTier::R2);
    LootWorldSnapshot w;
    w.OccupiedPointIds = {1, 2, 3};
    Rng rng(7);
    auto d = RunCycle(types, points, w, tuning, rng);
    CHECK_EQ(d.size(), size_t(7));
    for (const auto& s : d) CHECK(s.PointId > 3);
}

TEST(Loot_WeaponsCappedAtFifteenPercentOfBuildingPoints)
{
    const LootTuning tuning;
    CHECK_EQ(WeaponCapForBuilding(6, tuning), 0);
    CHECK_EQ(WeaponCapForBuilding(7, tuning), 1);
    CHECK_EQ(WeaponCapForBuilding(20, tuning), 3);
    CHECK_EQ(WeaponCapForBuilding(0, tuning), 0);

    std::vector<LootTypeDef> types = {MakeType("W_PM", 1000, 1000, Rarity::Common, true)};
    Rng rng(8);
    {   // 20-point building: exactly 3 weapon piles.
        auto points = MakeGrid(1, 20, RiskTier::R2);
        LootWorldSnapshot w;
        CHECK_EQ(RunCycle(types, points, w, tuning, rng).size(), size_t(3));
        w.WeaponPilesByBuilding[100] = 2;               // existing weapons count toward the cap
        CHECK_EQ(RunCycle(types, points, w, tuning, rng).size(), size_t(1));
        w.WeaponPilesByBuilding[100] = 3;
        CHECK(RunCycle(types, points, w, tuning, rng).empty());
    }
    {   // 6-point building: no weapons at all.
        auto points = MakeGrid(1, 6, RiskTier::R2);
        LootWorldSnapshot w;
        CHECK(RunCycle(types, points, w, tuning, rng).empty());
    }
    {   // Mixed: weapons capped, other items fill the remaining points.
        auto points = MakeGrid(1, 20, RiskTier::R2);
        std::vector<LootTypeDef> mixed = {MakeType("W_PM", 1000, 1000, Rarity::Common, true), MakeType("FOOD", 1000, 1000)};
        LootWorldSnapshot w;
        auto d = RunCycle(mixed, points, w, tuning, rng);
        CHECK_EQ(d.size(), size_t(20));
        int weapons = 0;
        for (const auto& s : d) weapons += s.IsWeapon ? 1 : 0;
        CHECK(weapons <= 3);
        CHECK(weapons > 0);
    }
}

TEST(Loot_RarityFollowsTierTable)
{
    // One type per rarity; nominal huge so all stay active. One point per building, fresh world each round.
    std::vector<LootTypeDef> types = {MakeType("C", 1000000, 1000000, Rarity::Common), MakeType("U", 1000000, 1000000, Rarity::Uncommon),
                                      MakeType("R", 1000000, 1000000, Rarity::Rare), MakeType("V", 1000000, 1000000, Rarity::VeryRare)};
    const LootTuning tuning;
    const struct { RiskTier tier; double expect[4]; } rows[] = {
        {RiskTier::R1, {0.85, 0.14, 0.01, 0.00}}, {RiskTier::R2, {0.60, 0.33, 0.06, 0.01}},
        {RiskTier::R3, {0.35, 0.45, 0.17, 0.03}}, {RiskTier::R4, {0.15, 0.40, 0.35, 0.10}}};
    for (const auto& row : rows)
    {
        auto points = MakeGrid(20000, 1, row.tier, 250.0);
        Rng rng(1234 + static_cast<int>(row.tier));
        LootWorldSnapshot w;
        auto d = RunCycle(types, points, w, tuning, rng);
        CHECK_EQ(d.size(), points.size());
        double n[4] = {0, 0, 0, 0};
        for (const auto& s : d) n[static_cast<int>(s.Rarity_)] += 1;
        for (int i = 0; i < 4; ++i) CHECK_NEAR(n[i] / d.size(), row.expect[i], 0.012);
        if (row.tier == RiskTier::R1) CHECK_EQ(n[3], 0.0);       // weight 0 => never
    }
}

TEST(Loot_TierSizeAndTagMatching)
{
    const LootTuning tuning;
    LootTypeDef militaryR4 = MakeType("W_SVD", 10, 10, Rarity::Rare, true);
    militaryR4.UsageTags = {"military"}; militaryR4.AllowedTiers = TierBit(RiskTier::R3) | TierBit(RiskTier::R4);
    LootTypeDef bigItem = MakeType("BACKPACK", 10, 10);
    bigItem.Size = LootSize::Large;
    LootTypeDef anywhere = MakeType("COIN", 10, 10); anywhere.UsageTags.clear();

    auto residential = MakeGrid(10, 5, RiskTier::R2);                 // wrong tag + wrong tier for SVD
    for (auto& p : residential) p.Size = LootSize::Small;             // too small for the backpack
    Rng rng(9);
    LootWorldSnapshot w;
    auto d = RunCycle({militaryR4, bigItem, anywhere}, residential, w, tuning, rng);
    for (const auto& s : d) CHECK(s.DefId == "COIN");
    CHECK_EQ(d.size(), size_t(10));

    auto barracks = MakeGrid(10, 10, RiskTier::R4, 300.0, "military");
    d = RunCycle({militaryR4}, barracks, w, tuning, rng);
    CHECK_EQ(d.size(), size_t(10));                                   // capped by nominal
    d = RunCycle({militaryR4}, MakeGrid(10, 10, RiskTier::R1, 300.0, "military"), w, tuning, rng);
    CHECK(d.empty());                                                 // R1 not allowed
}

TEST(Loot_DeterministicForSameSeed)
{
    const LootTuning tuning;
    std::vector<LootTypeDef> types = {MakeType("A", 40, 40), MakeType("B", 40, 40, Rarity::Rare)};
    auto points = MakeGrid(60, 4, RiskTier::R3);
    auto run = [&](uint64_t seed) {
        Rng rng(seed);
        LootWorldSnapshot w;
        auto d = RunCycle(types, points, w, tuning, rng);
        std::vector<std::pair<std::string, int64_t>> flat;
        for (const auto& s : d) flat.emplace_back(s.DefId, s.PointId);
        return flat;
    };
    CHECK(run(77) == run(77));
    CHECK(run(77) != run(78));
}

TEST(Loot_CycleIntervalGate)
{
    const LootTuning tuning;
    CHECK(!CycleDue(0, 299.9, tuning));
    CHECK(CycleDue(0, 300.0, tuning));
    CHECK(CycleDue(100, 1000, tuning));
}
