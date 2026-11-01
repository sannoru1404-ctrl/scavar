#include "Session.h"
#include <vector>

namespace Scavar
{
namespace
{
SessionCommand Cmd(CommandKind kind, AccountId account)
{
    SessionCommand c;
    c.Kind = kind;
    c.Account = account;
    return c;
}
} // namespace

SessionState SessionManager::State(AccountId account) const
{
    auto it = Entries.find(account);
    return it == Entries.end() ? SessionState::Offline : it->second.State;
}

SessionId SessionManager::CurrentSession(AccountId account) const
{
    auto it = Entries.find(account);
    return it == Entries.end() ? 0 : it->second.Session;
}

bool SessionManager::BodyInWorld(AccountId account) const { return State(account) != SessionState::Offline; }

bool SessionManager::InCombat(AccountId account, double now) const
{
    auto it = Entries.find(account);
    return it != Entries.end() && CombatRecent(it->second, now);
}

std::vector<SessionCommand> SessionManager::Login(AccountId account, double now, bool hasSavedCharacter, SessionId* outSession)
{
    std::vector<SessionCommand> out;
    Entry& e = Entries[account];
    const SessionId fresh = NextSession++;
    if (outSession) *outSession = fresh;

    switch (e.State)
    {
    case SessionState::Offline:
    {
        SessionCommand c = Cmd(hasSavedCharacter ? CommandKind::RestoreCharacter : CommandKind::SpawnCharacter, account);
        c.Session = fresh;
        c.FromWorld = false;
        out.push_back(c);
        e.LastCombat = -1e18;
        break;
    }
    case SessionState::Connected:
    case SessionState::LoggingOut:
    {
        SessionCommand evict = Cmd(CommandKind::EvictOldSession, account);
        evict.Session = e.Session;
        out.push_back(evict);
        if (e.State == SessionState::LoggingOut) out.push_back(Cmd(CommandKind::CancelLogout, account));
        SessionCommand c = Cmd(CommandKind::RestoreCharacter, account);
        c.Session = fresh;
        c.FromWorld = true;
        out.push_back(c);
        break;
    }
    case SessionState::Lingering:
    case SessionState::Grace:
    {
        SessionCommand c = Cmd(CommandKind::RestoreCharacter, account);
        c.Session = fresh;
        c.FromWorld = true;
        out.push_back(c);
        break;
    }
    }
    e.State = SessionState::Connected;
    e.Session = fresh;
    e.Deadline = 0.0;
    (void)now;
    return out;
}

std::vector<SessionCommand> SessionManager::LogoutRequest(AccountId account, SessionId session, double now)
{
    std::vector<SessionCommand> out;
    auto it = Entries.find(account);
    if (it == Entries.end() || it->second.State != SessionState::Connected || it->second.Session != session) return out;
    it->second.State = SessionState::LoggingOut;
    it->second.Deadline = now + Cfg.LogoutSeconds;
    return out;
}

std::vector<SessionCommand> SessionManager::Disconnect(AccountId account, SessionId session, double now)
{
    std::vector<SessionCommand> out;
    auto it = Entries.find(account);
    if (it == Entries.end()) return out;
    Entry& e = it->second;
    if (e.Session != session) return out;   // stale event from an evicted session
    if (e.State != SessionState::Connected && e.State != SessionState::LoggingOut) return out;

    if (CombatRecent(e, now))
    {
        e.State = SessionState::Lingering;
        e.Deadline = now + Cfg.LingerSeconds;
        SessionCommand keep = Cmd(CommandKind::KeepBodyInWorld, account);
        keep.Seconds = Cfg.LingerSeconds;
        keep.Vulnerable = true;
        out.push_back(keep);
    }
    else
    {
        e.State = SessionState::Grace;
        e.Deadline = now + Cfg.ReconnectWindowSeconds;
        SessionCommand keep = Cmd(CommandKind::KeepBodyInWorld, account);
        keep.Seconds = Cfg.ReconnectWindowSeconds;
        keep.Vulnerable = false;
        out.push_back(keep);
    }
    return out;
}

std::vector<SessionCommand> SessionManager::NotifyDamage(AccountId account, double now)
{
    std::vector<SessionCommand> out;
    auto it = Entries.find(account);
    if (it == Entries.end() || it->second.State == SessionState::Offline) return out;
    Entry& e = it->second;
    e.LastCombat = now;
    if (e.State == SessionState::LoggingOut)
    {
        e.State = SessionState::Connected;
        e.Deadline = 0.0;
        out.push_back(Cmd(CommandKind::CancelLogout, account));
    }
    return out;
}

void SessionManager::FinishLogout(AccountId account, Entry& e, double now, std::vector<SessionCommand>& out)
{
    if (CombatRecent(e, now))
    {
        e.State = SessionState::Lingering;
        e.Deadline = now + Cfg.LingerSeconds;
        SessionCommand keep = Cmd(CommandKind::KeepBodyInWorld, account);
        keep.Seconds = Cfg.LingerSeconds;
        keep.Vulnerable = true;
        out.push_back(keep);
    }
    else
    {
        SessionCommand rm = Cmd(CommandKind::RemoveCharacter, account);
        rm.Reason = RemoveReason::LogoutClean;
        out.push_back(rm);
        e.State = SessionState::Offline;
        e.Deadline = 0.0;
    }
}

std::vector<SessionCommand> SessionManager::Tick(double now)
{
    std::vector<SessionCommand> out;
    for (auto& [account, e] : Entries)
    {
        switch (e.State)
        {
        case SessionState::LoggingOut:
            if (now >= e.Deadline) FinishLogout(account, e, now, out);
            break;
        case SessionState::Lingering:
            if (now >= e.Deadline)
            {
                SessionCommand rm = Cmd(CommandKind::RemoveCharacter, account);
                rm.Reason = RemoveReason::LingerExpired;
                out.push_back(rm);
                e.State = SessionState::Offline;
            }
            break;
        case SessionState::Grace:
            if (now >= e.Deadline)
            {
                SessionCommand rm = Cmd(CommandKind::RemoveCharacter, account);
                rm.Reason = RemoveReason::GraceExpired;
                out.push_back(rm);
                e.State = SessionState::Offline;
            }
            break;
        case SessionState::Connected:
        case SessionState::Offline:
            break;
        }
    }
    return out;
}
} // namespace Scavar
