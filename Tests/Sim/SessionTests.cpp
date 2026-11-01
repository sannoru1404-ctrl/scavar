#include "TestFramework.h"
#include "Rng.h"
#include "Session.h"
#include <map>

using namespace Scavar;

namespace
{
int Count(const std::vector<SessionCommand>& cmds, CommandKind kind)
{
    int n = 0;
    for (const auto& c : cmds) n += c.Kind == kind;
    return n;
}
const SessionCommand* Find(const std::vector<SessionCommand>& cmds, CommandKind kind)
{
    for (const auto& c : cmds) if (c.Kind == kind) return &c;
    return nullptr;
}
}

TEST(AT001_SecondLoginEvictsTheFirstSession)
{
    SessionManager sm;
    SessionId s1 = 0, s2 = 0;
    auto first = sm.Login(1, 0.0, false, &s1);
    CHECK_EQ(first.size(), size_t(1));
    CHECK(first[0].Kind == CommandKind::SpawnCharacter);

    auto second = sm.Login(1, 5.0, true, &s2);
    CHECK(s2 != s1);
    CHECK_EQ(Count(second, CommandKind::SpawnCharacter), 0);   // never a second body
    const SessionCommand* evict = Find(second, CommandKind::EvictOldSession);
    const SessionCommand* restore = Find(second, CommandKind::RestoreCharacter);
    CHECK(evict && evict->Session == s1);
    CHECK(restore && restore->Session == s2 && restore->FromWorld);
    CHECK_EQ(sm.CurrentSession(1), s2);

    // A late disconnect from the evicted session must not touch the new one.
    CHECK(sm.Disconnect(1, s1, 6.0).empty());
    CHECK(sm.State(1) == SessionState::Connected);

    // Login while a logout countdown is running also cancels it.
    sm.LogoutRequest(1, s2, 10.0);
    CHECK(sm.State(1) == SessionState::LoggingOut);
    SessionId s3 = 0;
    auto third = sm.Login(1, 12.0, true, &s3);
    CHECK_EQ(Count(third, CommandKind::CancelLogout), 1);
    CHECK(sm.State(1) == SessionState::Connected);
    CHECK(sm.Tick(30.0).empty());
}

TEST(AT002_ReconnectWithinSixtySecondsReturnsToTheSameBody)
{
    SessionManager sm;
    SessionId s = 0;
    sm.Login(7, 0.0, false, &s);
    auto drop = sm.Disconnect(7, s, 100.0);
    const SessionCommand* keep = Find(drop, CommandKind::KeepBodyInWorld);
    CHECK(keep && keep->Seconds == 60.0 && !keep->Vulnerable);
    CHECK(sm.State(7) == SessionState::Grace);
    CHECK(sm.Tick(158.0).empty());

    SessionId s2 = 0;
    auto back = sm.Login(7, 159.0, true, &s2);
    CHECK_EQ(back.size(), size_t(1));
    CHECK(back[0].Kind == CommandKind::RestoreCharacter && back[0].FromWorld);
    CHECK(sm.Tick(400.0).empty());   // the old grace deadline no longer fires

    // Too late: the body is saved and removed, a later login loads the save (still the same character, not a new spawn).
    sm.Disconnect(7, s2, 500.0);
    CHECK(sm.Tick(559.9).empty());
    auto expired = sm.Tick(560.0);
    CHECK_EQ(expired.size(), size_t(1));
    CHECK(expired[0].Kind == CommandKind::RemoveCharacter && expired[0].Reason == RemoveReason::GraceExpired);
    CHECK(!sm.BodyInWorld(7));
    auto later = sm.Login(7, 561.0, true);
    CHECK(later[0].Kind == CommandKind::RestoreCharacter && !later[0].FromWorld);
}

TEST(AT003_DamageKeepsTheBodyInTheWorldForThirtySeconds)
{
    SessionManager sm;
    SessionId s = 0;
    sm.Login(1, 0.0, false, &s);
    sm.NotifyDamage(1, 100.0);
    auto cmds = sm.Disconnect(1, s, 105.0);
    const SessionCommand* keep = Find(cmds, CommandKind::KeepBodyInWorld);
    CHECK(keep && keep->Seconds == 30.0 && keep->Vulnerable);
    CHECK(sm.State(1) == SessionState::Lingering);
    CHECK(sm.Tick(134.9).empty());
    auto done = sm.Tick(135.0);
    CHECK_EQ(done.size(), size_t(1));
    CHECK(done[0].Kind == CommandKind::RemoveCharacter && done[0].Reason == RemoveReason::LingerExpired);

    // Reconnecting during the linger returns to the same vulnerable body.
    SessionId t = 0;
    sm.Login(2, 0.0, false, &t);
    sm.NotifyDamage(2, 50.0);
    sm.Disconnect(2, t, 51.0);
    SessionId t2 = 0;
    auto back = sm.Login(2, 60.0, true, &t2);
    CHECK(back.size() == 1 && back[0].Kind == CommandKind::RestoreCharacter && back[0].FromWorld);
}

TEST(AT003_LogoutCountdownRules)
{
    SessionManager sm;
    SessionId s = 0;
    sm.Login(1, 0.0, false, &s);

    // Clean logout without combat: saved and removed when the 10 s countdown ends.
    sm.LogoutRequest(1, s, 0.0);
    CHECK(sm.Tick(9.9).empty());
    auto done = sm.Tick(10.0);
    CHECK(done.size() == 1 && done[0].Kind == CommandKind::RemoveCharacter && done[0].Reason == RemoveReason::LogoutClean);
    CHECK(!sm.BodyInWorld(1));

    // Damage during the countdown cancels it.
    SessionId s2 = 0;
    sm.Login(1, 100.0, true, &s2);
    sm.LogoutRequest(1, s2, 100.0);
    auto cancel = sm.NotifyDamage(1, 105.0);
    CHECK_EQ(Count(cancel, CommandKind::CancelLogout), 1);
    CHECK(sm.State(1) == SessionState::Connected);
    CHECK(sm.Tick(110.0).empty());

    // Damage shortly before a logout: the body lingers 30 s, vulnerable, then is removed.
    sm.NotifyDamage(1, 200.0);
    sm.LogoutRequest(1, s2, 201.0);
    auto linger = sm.Tick(211.0);
    const SessionCommand* keep = Find(linger, CommandKind::KeepBodyInWorld);
    CHECK(keep && keep->Seconds == 30.0 && keep->Vulnerable);
    CHECK(sm.Tick(240.9).empty());
    CHECK_EQ(Count(sm.Tick(241.0), CommandKind::RemoveCharacter), 1);

    // Logout requests from a stale session id are ignored.
    SessionId s3 = 0;
    sm.Login(1, 300.0, true, &s3);
    CHECK(sm.LogoutRequest(1, s2, 301.0).empty());
    CHECK(sm.State(1) == SessionState::Connected);
}

TEST(Session_RandomEventsNeverCreateTwoBodiesForOneAccount)
{
    SessionManager sm;
    Rng rng(2024);
    std::map<AccountId, int> bodies;   // host simulation: bodies currently in the world
    double now = 0.0;
    int spawns = 0, restoresFromWorld = 0, removals = 0;

    auto apply = [&](const std::vector<SessionCommand>& cmds) {
        for (const auto& c : cmds)
        {
            int& n = bodies[c.Account];
            switch (c.Kind)
            {
            case CommandKind::SpawnCharacter: CHECK_EQ(n, 0); n = 1; ++spawns; break;
            case CommandKind::RestoreCharacter:
                if (c.FromWorld) { CHECK_EQ(n, 1); ++restoresFromWorld; }
                else { CHECK_EQ(n, 0); n = 1; }
                break;
            case CommandKind::RemoveCharacter: CHECK_EQ(n, 1); n = 0; ++removals; break;
            case CommandKind::KeepBodyInWorld: CHECK_EQ(n, 1); break;
            case CommandKind::EvictOldSession:
            case CommandKind::CancelLogout: break;
            }
        }
    };

    for (int i = 0; i < 20000; ++i)
    {
        now += rng.NextUnit() * 8.0;
        const AccountId a = 1 + rng.RangeInt(0, 4);
        switch (rng.RangeInt(0, 5))
        {
        case 0: apply(sm.Login(a, now, rng.Chance(0.8))); break;
        case 1: apply(sm.LogoutRequest(a, rng.Chance(0.9) ? sm.CurrentSession(a) : 999, now)); break;
        case 2: apply(sm.Disconnect(a, rng.Chance(0.9) ? sm.CurrentSession(a) : 999, now)); break;
        case 3: apply(sm.NotifyDamage(a, now)); break;
        default: apply(sm.Tick(now)); break;
        }
        for (AccountId acc = 1; acc <= 5; ++acc)
        {
            CHECK(bodies[acc] == 0 || bodies[acc] == 1);
            CHECK_EQ(bodies[acc] == 1, sm.BodyInWorld(acc));
        }
    }
    CHECK(spawns > 0 && restoresFromWorld > 0 && removals > 0);
}
