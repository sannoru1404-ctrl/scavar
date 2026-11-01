#pragma once
// Account session lifecycle (SYS-01): single live character per account, logout countdown, combat-log linger, reconnect.
// Pure state machine with an injected clock. The host executes the returned commands; every event/Tick returns the
// commands it caused. Invariant: at most one character body exists in the world per account at any time.
//
// Rules implemented (docs/04 SYS-01):
//  * Login while a session is live evicts the old session and re-attaches to the same body (AT-001).
//  * Clean logout: 10 s countdown, cancelled by damage. When it finishes: if damage was dealt/received in the last 30 s the body stays
//    30 s, vulnerable, and is then saved and removed; otherwise it is saved and removed at once.
//  * Connection loss: with combat in the last 30 s the body lingers 30 s, vulnerable; otherwise it is kept 60 s as a non-vulnerable grace
//    body so a reconnect returns to the SAME character (AT-002). After that it is saved and removed; a later login restores the save.
#include "Types.h"
#include <cstdint>
#include <map>
#include <vector>

namespace Scavar
{
using SessionId = int64_t;

struct SessionConfig
{
    double LogoutSeconds = 10.0;
    double CombatWindowSeconds = 30.0;
    double LingerSeconds = 30.0;
    double ReconnectWindowSeconds = 60.0;
};

enum class SessionState : uint8_t { Offline = 0, Connected, LoggingOut, Lingering, Grace };
enum class RemoveReason : uint8_t { LogoutClean = 0, LingerExpired, GraceExpired };
enum class CommandKind : uint8_t { SpawnCharacter = 0, RestoreCharacter, EvictOldSession, RemoveCharacter, KeepBodyInWorld, CancelLogout };

struct SessionCommand
{
    CommandKind Kind = CommandKind::SpawnCharacter;
    AccountId Account = InvalidId;
    SessionId Session = 0;          // new session (Spawn/Restore) or the evicted one (EvictOldSession)
    bool FromWorld = false;         // RestoreCharacter: true = re-attach to the body still in the world, false = load the saved character
    double Seconds = 0.0;           // KeepBodyInWorld
    bool Vulnerable = false;        // KeepBodyInWorld
    RemoveReason Reason = RemoveReason::LogoutClean;
};

class SessionManager
{
public:
    explicit SessionManager(SessionConfig cfg = {}) : Cfg(cfg) {}

    std::vector<SessionCommand> Login(AccountId account, double now, bool hasSavedCharacter, SessionId* outSession = nullptr);
    std::vector<SessionCommand> LogoutRequest(AccountId account, SessionId session, double now);
    std::vector<SessionCommand> Disconnect(AccountId account, SessionId session, double now);
    // Call for BOTH parties whenever damage is dealt or received.
    std::vector<SessionCommand> NotifyDamage(AccountId account, double now);
    std::vector<SessionCommand> Tick(double now);

    SessionState State(AccountId account) const;
    SessionId CurrentSession(AccountId account) const;
    bool BodyInWorld(AccountId account) const;
    bool InCombat(AccountId account, double now) const;

private:
    struct Entry
    {
        SessionState State = SessionState::Offline;
        SessionId Session = 0;
        double LastCombat = -1e18;
        double Deadline = 0.0;      // logout completion / linger end / grace end
    };

    bool CombatRecent(const Entry& e, double now) const { return now - e.LastCombat < Cfg.CombatWindowSeconds; }
    void FinishLogout(AccountId account, Entry& e, double now, std::vector<SessionCommand>& out);

    SessionConfig Cfg;
    std::map<AccountId, Entry> Entries;   // ordered: Tick output is deterministic
    SessionId NextSession = 1;
};
} // namespace Scavar
