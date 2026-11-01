#include "Ai.h"
#include <algorithm>
#include <cmath>

namespace Scavar
{
namespace
{
const ArchetypeDef kArchetypes[] = {
    //  id                       name          walk run  gmin gmax sightD sightN hear acc   react fire  shoot skit  flee retreat warn
    {AiArchetype::Militia,     "Militia",      1.4f, 4.0f, 1, 2, 150.f, 60.f, 1.0f, 0.60f, 1.0f, 0.40f, true,  false, 0.0f,  0.0f, 5.0f},
    {AiArchetype::RoadGuard,   "RoadGuard",    1.4f, 4.0f, 2, 3, 150.f, 60.f, 1.0f, 0.55f, 1.0f, 0.45f, true,  false, 0.0f,  0.0f, 6.0f},
    {AiArchetype::Marauder,    "Marauder",     1.6f, 5.0f, 2, 4, 150.f, 60.f, 1.0f, 0.35f, 1.0f, 0.50f, true,  false, 0.25f, 0.5f, 0.0f},
    {AiArchetype::Disoriented, "Disoriented",  1.2f, 3.0f, 1, 1, 150.f, 60.f, 1.0f, 0.00f, 1.0f, 1.00f, false, false, 0.0f,  0.0f, 0.0f},
    {AiArchetype::Wolf,        "Wolf",         2.0f, 7.0f, 2, 3, 100.f, 100.f, 1.5f, 0.00f, 0.8f, 1.00f, false, false, 0.5f,  0.0f, 0.0f},
    {AiArchetype::Deer,        "Deer",         1.8f, 9.0f, 1, 2, 120.f, 80.f, 1.5f, 0.00f, 0.8f, 1.00f, false, true,  0.0f,  0.0f, 0.0f},
};

constexpr double kEngageApproachM = 35.0;
constexpr double kArriveM = 3.0;
constexpr double kInvestigateGiveUpS = 8.0;
constexpr double kEngageLostS = 10.0;
constexpr double kRetreatCalmS = 10.0;
constexpr double kFleeCalmS = 5.0;
constexpr double kFleeSafeM = 80.0;
constexpr float kAwarenessDecayPerS = 10.f;

double Dist(const Vec3& a, const Vec3& b) { return (a - b).Length(); }

bool Contains(const std::vector<int64_t>& v, int64_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

AiAwareness& EntryFor(AiNpc& n, int64_t id)
{
    for (auto& e : n.Awareness)
        if (e.TargetId == id) return e;
    n.Awareness.push_back(AiAwareness{});
    n.Awareness.back().TargetId = id;
    return n.Awareness.back();
}

const AiTarget* FindTarget(const AiWorldView& v, int64_t id)
{
    for (const auto& t : v.Targets)
        if (t.Id == id) return &t;
    return nullptr;
}

void Enter(AiNpc& n, AiState s)
{
    n.State = s;
    n.StateTime = 0;
}

void Perceive(AiNpc& n, const ArchetypeDef& a, const AiWorldView& v, double dt, Rng& rng)
{
    for (auto& e : n.Awareness) { e.Seen = false; e.Perceived = false; }

    for (const AiTarget& t : v.Targets)
    {
        AiAwareness& e = EntryFor(n, t.Id);
        const double d = Dist(n.Pos, t.Pos);
        const float range = EffectiveSightRange(a, v.Night, t.Stance);
        const bool seen = d <= range && v.HasLineOfSight && v.HasLineOfSight(n.Pos, t.Pos);
        const bool heardSteps = d <= FootstepRadius(t.Move) * a.HearingScale && t.Move != MoveKind::Still;
        if (seen)
        {
            if (e.ReactionLeft < 0.f) e.ReactionLeft = static_cast<float>((0.4 + 0.6 * rng.NextUnit()) * a.ReactionScale);
            e.Seen = true;
            e.Perceived = true;
            n.LastKnown = t.Pos;
            n.HasLastKnown = true;
            e.ReactionLeft -= static_cast<float>(dt);
            if (e.ReactionLeft <= 0.f)
            {
                e.ReactionLeft = 0.f;   // expired; stays 0 so the delay is not rolled again (only <0 means "not started")
                const float gain = 40.f + 120.f * (1.f - static_cast<float>(d / range));
                e.Level = std::min(AwarenessEngage, e.Level + gain * static_cast<float>(dt));
            }
        }
        else if (heardSteps)
        {
            e.Perceived = true;
            e.Level = std::min(AwarenessEngage - 1.f, std::max(e.Level, AwarenessAlert));
            n.LastKnown = t.Pos;
            n.HasLastKnown = true;
        }
    }

    for (const AiSound& s : v.Sounds)
    {
        if (Dist(n.Pos, s.Pos) > GunshotRadius(s.Kind) * a.HearingScale) continue;
        AiAwareness& e = EntryFor(n, s.SourceId);
        e.Perceived = true;
        // Sound alone can raise suspicion up to 99; engaging needs eyes on the target or being attacked.
        e.Level = std::min(AwarenessEngage - 1.f, std::max(e.Level, AwarenessAlert) + 25.f);
        n.LastKnown = s.Pos;
        n.HasLastKnown = true;
    }

    for (int64_t attacker : n.AttackedBy)
    {
        AiAwareness& e = EntryFor(n, attacker);
        e.Perceived = true;
        e.Level = AwarenessEngage;
        if (e.ReactionLeft < 0.f) e.ReactionLeft = 0.f;
        if (const AiTarget* t = FindTarget(v, attacker))
        {
            n.LastKnown = t->Pos;
            n.HasLastKnown = true;
        }
    }

    for (auto& e : n.Awareness)
    {
        if (e.Perceived) continue;
        e.Level = std::max(0.f, e.Level - kAwarenessDecayPerS * static_cast<float>(dt));
        if (e.Level <= 0.f) e.ReactionLeft = -1.f;
    }
    n.Awareness.erase(std::remove_if(n.Awareness.begin(), n.Awareness.end(),
        [](const AiAwareness& e) { return e.Level <= 0.f && !e.Perceived && e.ReactionLeft < 0.f; }), n.Awareness.end());
}

const AiAwareness* StrongestAwareness(const AiNpc& n)
{
    const AiAwareness* best = nullptr;
    for (const auto& e : n.Awareness)
        if (e.Level > 0.f && (!best || e.Level > best->Level || (e.Level == best->Level && e.TargetId < best->TargetId))) best = &e;
    return best;
}
} // namespace

const ArchetypeDef& ArchetypeFor(AiArchetype a) { return kArchetypes[static_cast<size_t>(a)]; }

const char* ToString(AiState s)
{
    static const char* names[] = {"Idle", "Patrol", "Alert", "Investigate", "Warn", "Engage", "Retreat", "Flee"};
    return names[static_cast<size_t>(s)];
}

float EffectiveSightRange(const ArchetypeDef& a, bool night, TargetStance stance)
{
    const float base = night ? a.SightNightM : a.SightDayM;
    const float factor = stance == TargetStance::Crouch ? 0.7f : (stance == TargetStance::Prone ? 0.5f : 1.f);
    return base * factor;
}

float FootstepRadius(MoveKind m)
{
    switch (m)
    {
    case MoveKind::Sprint: return 25.f;
    case MoveKind::Walk: return 10.f;
    case MoveKind::Crouch: return 4.f;
    default: return 0.f;
    }
}

float GunshotRadius(SoundKind k) { return k == SoundKind::Gunshot ? 200.f : 80.f; }

void NotifyAttacked(AiNpc& npc, int64_t attackerId)
{
    if (!Contains(npc.AttackedBy, attackerId)) npc.AttackedBy.push_back(attackerId);
}

HostilityDecision DecideHostility(const HostilityInput& in)
{
    switch (in.Archetype)
    {
    case AiArchetype::Militia:
        // Law of the safe zone: only violators inside it, or whoever attacks the militia, are shot.
        if (in.TargetAttackedUs) return HostilityDecision::Attack;
        if (in.TargetInSafeZone && in.TargetViolatedLaw) return HostilityDecision::Attack;
        return HostilityDecision::Ignore;
    case AiArchetype::RoadGuard:
        if (in.TargetAttackedUs || in.TargetAttackedOthers) return HostilityDecision::Attack;
        if (in.TargetArmed && !in.Warned && in.Distance <= 25.f) return HostilityDecision::Warn;
        return HostilityDecision::Ignore;
    case AiArchetype::Marauder:
        if (in.GroupShouldRetreat) return HostilityDecision::Retreat;
        if (in.TargetAttackedUs) return HostilityDecision::Attack;
        if (in.Wounded) return HostilityDecision::Flee;
        if (in.GroupHasAdvantage && (in.TargetArmed || in.Distance < 60.f)) return HostilityDecision::Attack;
        return HostilityDecision::Avoid;
    case AiArchetype::Disoriented:
        if (in.TargetAttackedUs) return in.Cornered ? HostilityDecision::Attack : HostilityDecision::Flee;
        if (in.Distance <= 12.f) return HostilityDecision::AskHelp;
        return HostilityDecision::Ignore;
    case AiArchetype::Wolf:
        if (in.Wounded) return HostilityDecision::Flee;
        if (in.TargetInSafeZone) return HostilityDecision::Avoid;
        if (in.Distance <= (in.Night ? 60.f : 30.f)) return HostilityDecision::Attack;
        return HostilityDecision::Avoid;
    case AiArchetype::Deer:
        return HostilityDecision::Flee;
    }
    return HostilityDecision::Ignore;
}

std::vector<AiCommand> TickNpc(AiNpc& n, const AiGroupInfo& group, const AiWorldView& view, double dt, Rng& rng)
{
    std::vector<AiCommand> out;
    const ArchetypeDef& a = ArchetypeFor(n.Arch);
    n.StateTime += dt;
    n.FireCooldown -= dt;
    n.SpeechCooldown -= dt;

    Perceive(n, a, view, dt, rng);

    const AiAwareness* focus = StrongestAwareness(n);
    n.FocusTarget = focus ? focus->TargetId : 0;
    const float level = focus ? focus->Level : 0.f;
    const bool focusSeen = focus && focus->Seen;
    if (level >= AwarenessAlert) n.CalmTime = 0; else n.CalmTime += dt;
    if (focusSeen) n.UnseenTime = 0; else n.UnseenTime += dt;
    const AiTarget* ft = focus ? FindTarget(view, focus->TargetId) : nullptr;

    const double lossFraction = group.Total > 0 ? 1.0 - static_cast<double>(group.Alive) / group.Total : 0.0;
    const bool outnumbered = n.Arch == AiArchetype::Marauder && group.ThreatCount > group.Alive;
    const bool wounded = a.FleeHealthFraction > 0.f && n.HealthFraction < a.FleeHealthFraction;

    auto decide = [&]() -> HostilityDecision {
        if (!ft) return HostilityDecision::Ignore;
        HostilityInput in;
        in.Archetype = n.Arch;
        in.TargetInSafeZone = ft->InSafeZone;
        in.TargetViolatedLaw = ft->ViolatedLaw;
        in.TargetAttackedUs = Contains(n.AttackedBy, ft->Id);
        in.TargetAttackedOthers = ft->AttackedOthers;
        in.TargetArmed = ft->Armed;
        in.Warned = Contains(n.Warned, ft->Id);
        in.Distance = static_cast<float>(Dist(n.Pos, ft->Pos));
        in.Cornered = in.Distance < static_cast<float>(kArriveM);
        in.GroupHasAdvantage = group.Alive >= group.ThreatCount;
        in.GroupShouldRetreat = (a.RetreatLossFraction > 0.f && lossFraction > a.RetreatLossFraction) || outnumbered;
        in.Wounded = wounded;
        in.Night = view.Night;
        return DecideHostility(in);
    };

    auto moveTo = [&](const Vec3& p, bool run) {
        AiCommand c; c.Type = AiCommandType::MoveTo; c.Pos = p; c.Run = run; out.push_back(c);
    };
    auto fleeFrom = [&](const Vec3& threat) {
        AiCommand c; c.Type = AiCommandType::Flee; c.Pos = threat; c.Run = true; out.push_back(c);
    };
    auto report = [&]() {
        if (group.Total > 1)
        {
            AiCommand c; c.Type = AiCommandType::ReportAllies; c.Pos = n.LastKnown; c.TargetId = n.FocusTarget; out.push_back(c);
        }
    };
    auto speak = [&](SpeechKind k) {
        AiCommand c; c.Type = AiCommandType::Speak; c.Speech = k; c.TargetId = n.FocusTarget; out.push_back(c);
    };

    // Retreat / flee override everything except being already in those states.
    auto react = [&](HostilityDecision d) {
        switch (d)
        {
        case HostilityDecision::Retreat: Enter(n, AiState::Retreat); report(); break;
        case HostilityDecision::Flee: Enter(n, AiState::Flee); fleeFrom(n.HasLastKnown ? n.LastKnown : n.Pos); break;
        default: break;
        }
    };

    switch (n.State)
    {
    case AiState::Idle:
    case AiState::Patrol:
        if (level >= AwarenessAlert) { Enter(n, AiState::Alert); break; }
        if (!n.PatrolPoints.empty())
        {
            if (n.State == AiState::Idle) Enter(n, AiState::Patrol);
            if (Dist(n.Pos, n.PatrolPoints[n.PatrolIndex]) < kArriveM) n.PatrolIndex = (n.PatrolIndex + 1) % n.PatrolPoints.size();
            moveTo(n.PatrolPoints[n.PatrolIndex], false);
        }
        else if (n.State == AiState::Patrol) Enter(n, AiState::Idle);
        break;

    case AiState::Alert:
    {
        if (level < AwarenessAlert)
        {
            if (n.CalmTime > 3.0) Enter(n, n.HasLastKnown && !a.Skittish ? AiState::Investigate : AiState::Idle);
            break;
        }
        const HostilityDecision d = decide();
        switch (d)
        {
        case HostilityDecision::Warn: Enter(n, AiState::Warn); break;
        case HostilityDecision::AskHelp:
            if (n.SpeechCooldown <= 0) { speak(SpeechKind::AskHelp); n.SpeechCooldown = 10.0; }
            break;
        case HostilityDecision::Attack:
            if (level >= AwarenessEngage) { Enter(n, AiState::Engage); report(); }
            else if (!focusSeen && n.StateTime > 1.0 && n.HasLastKnown) Enter(n, AiState::Investigate);
            break;
        case HostilityDecision::Retreat:
        case HostilityDecision::Flee: react(d); break;
        case HostilityDecision::Avoid:
        case HostilityDecision::Ignore:
            if (n.StateTime > 4.0 && d == HostilityDecision::Ignore) Enter(n, AiState::Idle);
            break;
        }
        break;
    }

    case AiState::Investigate:
        if (level >= AwarenessAlert && focusSeen) { Enter(n, AiState::Alert); break; }
        if (n.HasLastKnown)
        {
            if (Dist(n.Pos, n.LastKnown) > kArriveM) moveTo(n.LastKnown, false);
        }
        if (n.StateTime > kInvestigateGiveUpS && level < AwarenessAlert) { n.HasLastKnown = false; Enter(n, AiState::Idle); }
        break;

    case AiState::Warn:
    {
        if (n.StateTime <= dt && ft && !Contains(n.Warned, ft->Id))
        {
            speak(SpeechKind::Warn);
            n.Warned.push_back(ft->Id);
        }
        const HostilityDecision d = decide();
        if (d == HostilityDecision::Attack && level >= AwarenessEngage) { Enter(n, AiState::Engage); report(); break; }
        if (level < AwarenessAlert && n.CalmTime > 3.0) { Enter(n, AiState::Idle); break; }
        if (n.StateTime >= a.WarnSeconds) Enter(n, AiState::Alert);
        break;
    }

    case AiState::Engage:
    {
        const HostilityDecision d = decide();
        if (d == HostilityDecision::Retreat || d == HostilityDecision::Flee) { react(d); break; }
        if (n.UnseenTime > kEngageLostS) { Enter(n, AiState::Investigate); break; }
        if (ft && focusSeen)
        {
            const double dist = Dist(n.Pos, ft->Pos);
            if (a.CanShoot)
            {
                if (n.FireCooldown <= 0)
                {
                    AiCommand c; c.Type = AiCommandType::Shoot; c.TargetId = ft->Id; c.Pos = ft->Pos; c.Accuracy = a.Accuracy;
                    out.push_back(c);
                    n.FireCooldown = a.FireIntervalS;
                }
                if (dist > kEngageApproachM) moveTo(ft->Pos, true);
            }
            else moveTo(ft->Pos, true); // melee predators close in
        }
        else if (n.HasLastKnown) moveTo(n.LastKnown, false);
        break;
    }

    case AiState::Retreat:
        if (n.StateTime <= dt || std::fmod(n.StateTime, 2.0) < dt) moveTo(n.Home, true);
        if (n.CalmTime > kRetreatCalmS) { n.AttackedBy.clear(); Enter(n, AiState::Idle); }
        break;

    case AiState::Flee:
    {
        if (std::fmod(n.StateTime, 1.0) < dt || n.StateTime <= dt) fleeFrom(n.HasLastKnown ? n.LastKnown : n.Pos);
        const bool far = !n.HasLastKnown || Dist(n.Pos, n.LastKnown) > kFleeSafeM;
        if (n.CalmTime > kFleeCalmS && far) { n.AttackedBy.clear(); Enter(n, AiState::Idle); }
        break;
    }
    }

    // Skittish animals bolt the moment anything alarms them, whatever state they were in.
    if (a.Skittish && level >= AwarenessAlert && n.State != AiState::Flee) { Enter(n, AiState::Flee); fleeFrom(n.LastKnown); }
    return out;
}
} // namespace Scavar
