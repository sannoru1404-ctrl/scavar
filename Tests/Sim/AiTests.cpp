#include "TestFramework.h"
#include "Ai.h"
#include "AiDirector.h"
#include <chrono>
#include <set>

using namespace Scavar;

namespace
{
AiTarget MakeTarget(int64_t id, double x, double y = 0)
{
    AiTarget t; t.Id = id; t.Pos = Vec3{x, y, 0}; return t;
}

AiWorldView MakeView(std::vector<AiTarget> targets, bool los = true, bool night = false)
{
    AiWorldView v;
    v.Night = night;
    v.Targets = std::move(targets);
    v.HasLineOfSight = [los](const Vec3&, const Vec3&) { return los; };
    return v;
}

AiNpc MakeNpc(AiArchetype a, Vec3 pos = {})
{
    AiNpc n; n.Id = 1; n.Arch = a; n.Pos = pos; n.Home = Vec3{-50, 0, 0}; return n;
}

struct Runner
{
    AiNpc Npc;
    AiGroupInfo Group;
    Rng Brain{12345};
    double Time = 0;
    int Shots = 0, Warns = 0, AskHelps = 0, Reports = 0, Flees = 0, Moves = 0;
    std::set<AiState> Visited;

    explicit Runner(AiArchetype a, Vec3 pos = {}) : Npc(MakeNpc(a, pos)) {}

    std::vector<AiCommand> Step(AiWorldView view, double dt = 0.1)
    {
        Time += dt;
        view.Time = Time;
        auto cmds = TickNpc(Npc, Group, view, dt, Brain);
        for (const auto& c : cmds)
        {
            Shots += c.Type == AiCommandType::Shoot;
            Warns += c.Type == AiCommandType::Speak && c.Speech == SpeechKind::Warn;
            AskHelps += c.Type == AiCommandType::Speak && c.Speech == SpeechKind::AskHelp;
            Reports += c.Type == AiCommandType::ReportAllies;
            Flees += c.Type == AiCommandType::Flee;
            Moves += c.Type == AiCommandType::MoveTo;
        }
        Visited.insert(Npc.State);
        return cmds;
    }

    void Run(const AiWorldView& view, double seconds, double dt = 0.1)
    {
        for (double t = 0; t < seconds; t += dt) Step(view, dt);
    }

    float Level(int64_t target = 1) const
    {
        for (const auto& e : Npc.Awareness) if (e.TargetId == target) return e.Level;
        return 0.f;
    }
    bool Was(AiState s) const { return Visited.count(s) > 0; }
};
}

TEST(Ai_ArchetypeTableIsConsistent)
{
    for (int i = 0; i <= static_cast<int>(AiArchetype::Deer); ++i)
    {
        const ArchetypeDef& a = ArchetypeFor(static_cast<AiArchetype>(i));
        CHECK(static_cast<int>(a.Id) == i);
        CHECK(a.WalkSpeed > 0 && a.RunSpeed > a.WalkSpeed);
        CHECK(a.GroupMin >= 1 && a.GroupMax >= a.GroupMin);
        CHECK(a.SightDayM > 0 && a.SightNightM > 0);
    }
    CHECK(!ArchetypeFor(AiArchetype::Disoriented).CanShoot);
    CHECK(!ArchetypeFor(AiArchetype::Wolf).CanShoot);
    CHECK(ArchetypeFor(AiArchetype::Marauder).CanShoot);
}

TEST(Ai_AwarenessRisesAfterReactionDelayAndDecays)
{
    Runner r(AiArchetype::Marauder);
    const AiWorldView seen = MakeView({MakeTarget(1, 50)});
    r.Run(seen, 0.3);
    CHECK_NEAR(r.Level(), 0.0, 1e-6);   // reaction delay is at least 0.4 s
    r.Run(seen, 2.2);
    CHECK_NEAR(r.Level(), 100.0, 1e-6);
    const AiWorldView blind = MakeView({MakeTarget(1, 50)}, false);
    r.Run(blind, 5.0);
    CHECK_NEAR(r.Level(), 50.0, 1.5);   // 10 per second decay
    r.Run(blind, 6.0);
    CHECK_NEAR(r.Level(), 0.0, 1e-6);
    CHECK(r.Npc.Awareness.empty());
}

TEST(Ai_SightRangeDependsOnStanceAndDaylight)
{
    auto sees = [](double dist, TargetStance st, bool night) {
        Runner r(AiArchetype::Marauder);
        AiTarget t = MakeTarget(1, dist); t.Stance = st;
        r.Run(MakeView({t}, true, night), 3.0);
        return r.Level() > 0.f;
    };
    CHECK(sees(140, TargetStance::Standing, false));
    CHECK(!sees(160, TargetStance::Standing, false));
    CHECK(!sees(80, TargetStance::Prone, false));    // 150 * 0.5 = 75
    CHECK(sees(70, TargetStance::Prone, false));
    CHECK(sees(100, TargetStance::Crouch, false));   // 150 * 0.7 = 105
    CHECK(!sees(110, TargetStance::Crouch, false));
    CHECK(!sees(70, TargetStance::Standing, true));  // night: 60
    CHECK(sees(55, TargetStance::Standing, true));
}

TEST(Ai_HearingRadiiForShotsAndFootsteps)
{
    auto shotLevel = [](double dist, SoundKind k) {
        Runner r(AiArchetype::Marauder);
        AiWorldView v = MakeView({}, false);
        v.Sounds.push_back(AiSound{Vec3{dist, 0, 0}, k, 7});
        r.Step(v);
        return r.Level(7);
    };
    CHECK(shotLevel(150, SoundKind::Gunshot) >= AwarenessAlert);
    CHECK_NEAR(shotLevel(250, SoundKind::Gunshot), 0.0, 1e-6);
    CHECK_NEAR(shotLevel(150, SoundKind::GunshotSuppressed), 0.0, 1e-6);
    CHECK(shotLevel(70, SoundKind::GunshotSuppressed) >= AwarenessAlert);

    auto stepLevel = [](double dist, MoveKind m) {
        Runner r(AiArchetype::Marauder);
        AiTarget t = MakeTarget(1, dist); t.Move = m;
        r.Step(MakeView({t}, false));
        return r.Level();
    };
    CHECK(stepLevel(20, MoveKind::Sprint) >= AwarenessAlert);
    CHECK_NEAR(stepLevel(20, MoveKind::Walk), 0.0, 1e-6);
    CHECK(stepLevel(8, MoveKind::Walk) >= AwarenessAlert);
    CHECK(stepLevel(3, MoveKind::Crouch) >= AwarenessAlert);
    CHECK_NEAR(stepLevel(5, MoveKind::Crouch), 0.0, 1e-6);
    CHECK_NEAR(stepLevel(1, MoveKind::Still), 0.0, 1e-6);
}

TEST(Ai_SoundAloneNeverReachesEngage)
{
    Runner r(AiArchetype::Marauder);
    for (int i = 0; i < 40; ++i)
    {
        AiWorldView v = MakeView({}, false);
        v.Sounds.push_back(AiSound{Vec3{30, 0, 0}, SoundKind::Gunshot, 9});
        r.Step(v);
    }
    CHECK(r.Level(9) < AwarenessEngage);
    CHECK(!r.Was(AiState::Engage));
    CHECK_EQ(r.Shots, 0);
    CHECK(r.Was(AiState::Alert) || r.Was(AiState::Investigate));
}

// AT-024
TEST(AT024_NoReactionWithoutLineOfSightOrSound)
{
    Rng scenario(777);
    for (int a = 0; a <= static_cast<int>(AiArchetype::Deer); ++a)
    {
        Runner r(static_cast<AiArchetype>(a));
        for (int tick = 0; tick < 500; ++tick)
        {
            std::vector<AiTarget> targets;
            for (int i = 1; i <= 5; ++i)
            {
                AiTarget t = MakeTarget(i, scenario.NextUnit() * 400 - 200, scenario.NextUnit() * 400 - 200);
                t.Stance = TargetStance::Standing;
                t.Move = MoveKind::Still;     // silent
                t.Armed = true;
                targets.push_back(t);
            }
            auto cmds = r.Step(MakeView(targets, /*los=*/false));
            CHECK(cmds.empty());
        }
        CHECK(r.Npc.Awareness.empty());
        CHECK(r.Npc.State == AiState::Idle);
    }
    // far away but with line of sight: beyond sight radius is still nothing
    Runner far(AiArchetype::Marauder);
    far.Run(MakeView({MakeTarget(1, 400)}), 20.0);
    CHECK(far.Npc.Awareness.empty());
}

TEST(Ai_MilitiaShootsOnlyViolatorsInSafeZone)
{
    {   // violator inside the safe zone
        Runner r(AiArchetype::Militia);
        AiTarget t = MakeTarget(1, 30); t.InSafeZone = true; t.ViolatedLaw = true; t.Armed = true;
        r.Run(MakeView({t}), 6.0);
        CHECK(r.Was(AiState::Engage));
        CHECK(r.Shots > 0);
    }
    {   // armed but lawful inside the safe zone
        Runner r(AiArchetype::Militia);
        AiTarget t = MakeTarget(1, 30); t.InSafeZone = true; t.Armed = true;
        r.Run(MakeView({t}), 30.0);
        CHECK(!r.Was(AiState::Engage));
        CHECK_EQ(r.Shots, 0);
    }
    {   // violator outside the safe zone is not the militia's business
        Runner r(AiArchetype::Militia);
        AiTarget t = MakeTarget(1, 30); t.ViolatedLaw = true; t.Armed = true;
        r.Run(MakeView({t}), 30.0);
        CHECK_EQ(r.Shots, 0);
    }
    {   // anyone who attacks the militia is fair game
        Runner r(AiArchetype::Militia);
        NotifyAttacked(r.Npc, 1);
        r.Run(MakeView({MakeTarget(1, 30)}), 4.0);
        CHECK(r.Was(AiState::Engage));
        CHECK(r.Shots > 0);
    }
}

TEST(Ai_RoadGuardWarnsThenShootsOnlyAggressors)
{
    {
        Runner r(AiArchetype::RoadGuard);
        AiTarget t = MakeTarget(1, 20); t.Armed = true;
        r.Run(MakeView({t}), 40.0);
        CHECK(r.Was(AiState::Warn));
        CHECK_EQ(r.Warns, 1);
        CHECK_EQ(r.Shots, 0);
        CHECK(!r.Was(AiState::Engage));
    }
    {
        Runner r(AiArchetype::RoadGuard);
        AiTarget t = MakeTarget(1, 40); t.AttackedOthers = true;
        r.Run(MakeView({t}), 5.0);
        CHECK(r.Was(AiState::Engage));
        CHECK(r.Shots > 0);
    }
}

TEST(Ai_MarauderEngagesWithAdvantageAndRetreatsWhenWeak)
{
    {
        Runner r(AiArchetype::Marauder);
        r.Group = AiGroupInfo{3, 3, 1};
        AiTarget t = MakeTarget(1, 40); t.Armed = true;
        r.Run(MakeView({t}), 6.0);
        CHECK(r.Was(AiState::Engage));
        CHECK(r.Shots > 0);
        CHECK(r.Reports > 0);
    }
    {   // outnumbered
        Runner r(AiArchetype::Marauder);
        r.Group = AiGroupInfo{2, 2, 4};
        AiTarget t = MakeTarget(1, 40); t.Armed = true;
        r.Run(MakeView({t}), 6.0);
        CHECK(r.Was(AiState::Retreat));
        CHECK(!r.Was(AiState::Engage));
        CHECK_EQ(r.Shots, 0);
        CHECK(r.Reports > 0);
        r.Run(MakeView({}, false), 20.0);       // target gone: calms down
        CHECK(r.Npc.State == AiState::Idle);
    }
    {   // lost more than half of the group
        Runner r(AiArchetype::Marauder);
        r.Group = AiGroupInfo{4, 1, 1};
        AiTarget t = MakeTarget(1, 40); t.Armed = true;
        r.Run(MakeView({t}), 6.0);
        CHECK(r.Was(AiState::Retreat));
        CHECK_EQ(r.Shots, 0);
    }
}

TEST(Ai_WolfAttacksByNightFleesWhenWounded)
{
    {
        Runner r(AiArchetype::Wolf);
        r.Run(MakeView({MakeTarget(1, 50)}, true, /*night=*/true), 6.0);
        CHECK(r.Was(AiState::Engage));
        CHECK_EQ(r.Shots, 0);                  // melee predator
        CHECK(r.Moves > 0);
    }
    {
        Runner r(AiArchetype::Wolf);           // by day only a very close person triggers it
        r.Run(MakeView({MakeTarget(1, 45)}), 8.0);
        CHECK(!r.Was(AiState::Engage));
    }
    {
        Runner r(AiArchetype::Wolf);
        r.Npc.HealthFraction = 0.3f;
        r.Run(MakeView({MakeTarget(1, 25)}), 4.0);
        CHECK(r.Was(AiState::Flee));
        CHECK(!r.Was(AiState::Engage));
        CHECK(r.Flees > 0);
    }
}

TEST(Ai_DeerFleesAtFirstSight)
{
    Runner r(AiArchetype::Deer);
    r.Run(MakeView({MakeTarget(1, 100)}), 3.0);
    CHECK(r.Was(AiState::Flee));
    CHECK(r.Flees > 0);
    CHECK_EQ(r.Shots, 0);
}

TEST(Ai_DisorientedResidentIsHarmlessUnlessCornered)
{
    {   // never aggressive without provocation
        Rng scenario(5);
        Runner r(AiArchetype::Disoriented);
        for (int tick = 0; tick < 1000; ++tick)
        {
            AiTarget t = MakeTarget(1, scenario.NextUnit() * 80 - 40, scenario.NextUnit() * 80 - 40);
            t.Armed = scenario.Chance(0.5);
            t.Move = static_cast<MoveKind>(scenario.RangeInt(0, 3));
            r.Step(MakeView({t}));
        }
        CHECK(!r.Was(AiState::Engage));
        CHECK(!r.Was(AiState::Flee));
        CHECK_EQ(r.Shots, 0);
    }
    {   // asks for help when somebody is close
        Runner r(AiArchetype::Disoriented);
        r.Run(MakeView({MakeTarget(1, 8)}), 5.0);
        CHECK(r.AskHelps >= 1);
        CHECK_EQ(r.Shots, 0);
    }
    {   // provoked from a distance: runs away
        Runner r(AiArchetype::Disoriented);
        NotifyAttacked(r.Npc, 1);
        r.Run(MakeView({MakeTarget(1, 20)}), 3.0);
        CHECK(r.Was(AiState::Flee));
        CHECK(!r.Was(AiState::Engage));
    }
    {   // provoked and cornered: may defend itself, but it has no firearm
        Runner r(AiArchetype::Disoriented);
        NotifyAttacked(r.Npc, 1);
        r.Run(MakeView({MakeTarget(1, 2)}), 3.0);
        CHECK(r.Was(AiState::Engage));
        CHECK_EQ(r.Shots, 0);
    }
}

TEST(Ai_PatrolFollowsWaypoints)
{
    Runner r(AiArchetype::Militia);
    r.Npc.PatrolPoints = {Vec3{10, 0, 0}, Vec3{10, 10, 0}};
    auto cmds = r.Step(MakeView({}, false));
    CHECK(r.Npc.State == AiState::Patrol);
    CHECK_EQ(cmds.size(), size_t(1));
    CHECK(cmds[0].Type == AiCommandType::MoveTo);
    CHECK_NEAR(cmds[0].Pos.x, 10, 1e-9);
    r.Npc.Pos = Vec3{10, 0, 0};            // arrived at the first point
    cmds = r.Step(MakeView({}, false));
    CHECK_NEAR(cmds[0].Pos.y, 10, 1e-9);
}

// AT-023: identical decisions regardless of the impairment trait.
TEST(AT023_ImpairmentNeverChangesDecisions)
{
    int differences = 0;
    for (int run = 0; run < 1000; ++run)
    {
        Rng scenario(1000 + static_cast<uint64_t>(run));
        const AiArchetype arch = static_cast<AiArchetype>(run % 6);
        Runner a(arch), b(arch);
        a.Npc.ImpairmentLevel = 0;
        b.Npc.ImpairmentLevel = 1 + run % 4;
        a.Group = b.Group = AiGroupInfo{3, 2 + run % 2, 1 + run % 3};
        if (run % 5 == 0) { NotifyAttacked(a.Npc, 1); NotifyAttacked(b.Npc, 1); }
        for (int tick = 0; tick < 60; ++tick)
        {
            AiTarget t = MakeTarget(1, scenario.NextUnit() * 120 - 60, scenario.NextUnit() * 120 - 60);
            t.Armed = scenario.Chance(0.5);
            t.InSafeZone = scenario.Chance(0.3);
            t.ViolatedLaw = scenario.Chance(0.3);
            t.AttackedOthers = scenario.Chance(0.2);
            t.Move = static_cast<MoveKind>(scenario.RangeInt(0, 3));
            AiWorldView v = MakeView({t}, scenario.Chance(0.8), scenario.Chance(0.4));
            const auto ca = a.Step(v);
            const auto cb = b.Step(v);
            if (!(ca == cb) || a.Npc.State != b.Npc.State || a.Level() != b.Level()) ++differences;
        }
    }
    CHECK_EQ(differences, 0);
}

TEST(Ai_DecideHostilityIsPureAndTotal)
{
    HostilityInput in;
    for (int a = 0; a <= static_cast<int>(AiArchetype::Deer); ++a)
    {
        in.Archetype = static_cast<AiArchetype>(a);
        const HostilityDecision d1 = DecideHostility(in);
        const HostilityDecision d2 = DecideHostility(in);
        CHECK(d1 == d2);
    }
}

// ---- Director (AT-026) ----
namespace
{
AiZone MakeZone(std::vector<Vec3> points, int maxGroups = 2)
{
    AiZone z; z.Id = 1; z.MaxGroups = maxGroups; z.SpawnPoints = std::move(points);
    z.Templates.push_back(AiGroupTemplate{AiArchetype::Marauder, 2, 3});
    return z;
}
DirectorWorld MakeWorld(double now, std::vector<Vec3> players, std::function<bool(const Vec3&)> visible)
{
    DirectorWorld w; w.Now = now; w.Players = std::move(players); w.VisibleToAnyPlayer = std::move(visible); return w;
}
}

TEST(AT026_SpawnsOnlyInRangeAndOutOfSight)
{
    const Vec3 tooClose{100, 0, 0}, visible{300, 0, 0}, good{0, 350, 0}, tooFar{900, 0, 0};
    auto isVisible = [&](const Vec3& p) { return p.x == visible.x && p.y == visible.y; };
    DirectorState st; Rng rng(1); DirectorTuning tune;
    std::vector<AiZone> zones{MakeZone({tooClose, visible, good, tooFar}, 1)};
    const auto out = DirectorTick(st, zones, MakeWorld(0, {Vec3{0, 0, 0}}, isVisible), tune, rng);
    CHECK_EQ(out.Spawns.size(), size_t(1));
    if (!out.Spawns.empty()) { CHECK_NEAR(out.Spawns[0].Pos.y, 350, 1e-9); CHECK(out.Spawns[0].Size >= 2 && out.Spawns[0].Size <= 3); }
    // zone already full
    const auto again = DirectorTick(st, zones, MakeWorld(1, {Vec3{0, 0, 0}}, isVisible), tune, rng);
    CHECK(again.Spawns.empty());
}

TEST(AT026_NeverSpawnsInViewOverManyTrials)
{
    Rng rng(99);
    for (int trial = 0; trial < 500; ++trial)
    {
        std::vector<Vec3> pts;
        for (int i = 0; i < 6; ++i) pts.push_back(Vec3{rng.NextUnit() * 800 - 400, rng.NextUnit() * 800 - 400, 0});
        const Vec3 player{rng.NextUnit() * 100 - 50, rng.NextUnit() * 100 - 50, 0};
        auto vis = [&](const Vec3& p) { return std::fmod(std::fabs(p.x) + std::fabs(p.y), 3.0) < 1.5; };
        DirectorState st; DirectorTuning tune;
        const auto out = DirectorTick(st, {MakeZone(pts, 3)}, MakeWorld(0, {player}, vis), tune, rng);
        for (const auto& s : out.Spawns)
        {
            const double d = (s.Pos - player).Length();
            CHECK(!vis(s.Pos));
            CHECK(d >= tune.MinPlayerDistanceM && d <= tune.ActivationRadiusM);
        }
    }
}

TEST(AT026_EmptyVisibilityPredicateBlocksSpawning)
{
    DirectorState st; Rng rng(2);
    DirectorWorld w; w.Now = 0; w.Players = {Vec3{0, 0, 0}};
    CHECK(DirectorTick(st, {MakeZone({Vec3{0, 300, 0}})}, w, DirectorTuning{}, rng).Spawns.empty());
}

TEST(AT026_GlobalCapIsHard)
{
    DirectorState st; Rng rng(3); DirectorTuning tune; tune.GlobalNpcCap = 5;
    AiZone z = MakeZone({Vec3{0, 300, 0}, Vec3{0, -300, 0}}, 10);
    z.Templates = {AiGroupTemplate{AiArchetype::Marauder, 4, 4}};
    auto hidden = [](const Vec3&) { return false; };
    for (int i = 0; i < 10; ++i) DirectorTick(st, {z}, MakeWorld(i, {Vec3{0, 0, 0}}, hidden), tune, rng);
    CHECK(st.ActiveNpcCount() <= 5);
    CHECK_EQ(st.Groups.size(), size_t(1));
}

TEST(AT026_DespawnAfterTwoMinutesWithoutPlayersAndNoCooldown)
{
    DirectorState st; Rng rng(4); DirectorTuning tune;
    auto hidden = [](const Vec3&) { return false; };
    std::vector<AiZone> zones{MakeZone({Vec3{0, 300, 0}}, 1)};
    DirectorTick(st, zones, MakeWorld(0, {Vec3{0, 0, 0}}, hidden), tune, rng);
    CHECK_EQ(st.Groups.size(), size_t(1));
    const std::vector<Vec3> away{Vec3{2000, 0, 0}};
    DirectorOutput out;
    for (double t = 10; t < 115; t += 5) out = DirectorTick(st, zones, MakeWorld(t, away, hidden), tune, rng);
    CHECK_EQ(st.Groups.size(), size_t(1));          // not yet
    out = DirectorTick(st, zones, MakeWorld(135, away, hidden), tune, rng);
    CHECK_EQ(out.Despawns.size(), size_t(1));
    CHECK(st.Groups.empty());
    // a player coming back gets a fresh group immediately: despawn is not a kill
    out = DirectorTick(st, zones, MakeWorld(140, {Vec3{0, 0, 0}}, hidden), tune, rng);
    CHECK_EQ(out.Spawns.size(), size_t(1));
}

TEST(AT026_KilledGroupRespawnsOnlyAfterCooldown)
{
    DirectorState st; Rng rng(5); DirectorTuning tune;
    auto hidden = [](const Vec3&) { return false; };
    std::vector<AiZone> zones{MakeZone({Vec3{0, 300, 0}}, 1)};
    const std::vector<Vec3> player{Vec3{0, 0, 0}};
    DirectorTick(st, zones, MakeWorld(0, player, hidden), tune, rng);
    DirectorGroupKilled(st, zones, 1, 100.0, rng);
    CHECK(st.Groups.empty());
    CHECK(DirectorTick(st, zones, MakeWorld(100 + 29 * 60, player, hidden), tune, rng).Spawns.empty());
    CHECK(DirectorTick(st, zones, MakeWorld(100 + 61 * 60, player, hidden), tune, rng).Spawns.size() == 1);
}

TEST(AT025_EightyNpcsFitTheTickBudget)
{
    std::vector<Runner> npcs;
    for (int i = 0; i < 80; ++i) npcs.emplace_back(static_cast<AiArchetype>(i % 6), Vec3{i * 3.0, 0, 0});
    AiWorldView view;
    view.HasLineOfSight = [](const Vec3& a, const Vec3& b) { return (a - b).Length() < 120.0; };
    for (int i = 1; i <= 8; ++i)
    {
        AiTarget t = MakeTarget(i, i * 20.0, i * 5.0); t.Move = MoveKind::Walk; t.Armed = true; view.Targets.push_back(t);
    }
    const int ticks = 100;
    const auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < ticks; ++t)
        for (auto& n : npcs) n.Step(view, 0.033);
    const double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const double perNpcMs = totalMs / (ticks * 80.0);
    std::printf("    AT-025: %.4f ms per NPC tick (budget 4 ms), %.2f ms for all 80 per frame\n", perNpcMs, perNpcMs * 80.0);
    CHECK(perNpcMs <= 4.0);
}
