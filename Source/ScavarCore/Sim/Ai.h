#pragma once
// SYS-08 / SYS-13 NPC brain: perception without omniscience, awareness meter, state machine, hostility decision.
// Pure logic, deterministic given an Rng. The engine executes the returned commands and feeds positions/LOS back.
//
// NoDiagnosisHostility (MUST): AiNpc::ImpairmentLevel exists only as a flavour trait for animation/dialogue.
// Nothing in Ai.cpp reads it, and DecideHostility() has no parameter that could carry it.
#include "Rng.h"
#include "Types.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace Scavar
{
enum class AiArchetype : uint8_t { Militia = 0, RoadGuard, Marauder, Disoriented, Wolf, Deer };
enum class AiState : uint8_t { Idle = 0, Patrol, Alert, Investigate, Warn, Engage, Retreat, Flee };

constexpr float AwarenessAlert = 30.f;
constexpr float AwarenessEngage = 100.f;

struct ArchetypeDef
{
    AiArchetype Id;
    const char* Name;
    float WalkSpeed, RunSpeed;            // m/s
    int GroupMin, GroupMax;
    float SightDayM, SightNightM;
    float HearingScale;                   // multiplies every hearing radius
    float Accuracy;                       // hit probability handed to the engine with Shoot
    float ReactionScale;                  // multiplies the 0.4..1.0 s reaction delay
    float FireIntervalS;
    bool CanShoot;
    bool Skittish;                        // flees as soon as it notices a person
    float FleeHealthFraction;             // flee below this health fraction (<=0 never)
    float RetreatLossFraction;            // retreat when group losses exceed this (<=0 never)
    float WarnSeconds;
};
const ArchetypeDef& ArchetypeFor(AiArchetype a);
const char* ToString(AiState s);

enum class TargetStance : uint8_t { Standing = 0, Crouch, Prone };
enum class MoveKind : uint8_t { Still = 0, Crouch, Walk, Sprint };
enum class SoundKind : uint8_t { Gunshot = 0, GunshotSuppressed };

struct AiTarget
{
    int64_t Id = 0;
    Vec3 Pos;
    TargetStance Stance = TargetStance::Standing;
    MoveKind Move = MoveKind::Still;
    bool InSafeZone = false;
    bool Armed = false;
    bool ViolatedLaw = false;     // fired a weapon / attacked somebody inside a safe zone
    bool AttackedOthers = false;  // observed attacking a person
};

struct AiSound
{
    Vec3 Pos;
    SoundKind Kind = SoundKind::Gunshot;
    int64_t SourceId = 0;
};

using LineOfSightFn = std::function<bool(const Vec3& from, const Vec3& to)>;

struct AiWorldView
{
    double Time = 0;
    bool Night = false;
    std::vector<AiTarget> Targets;
    std::vector<AiSound> Sounds;          // events of this tick only
    LineOfSightFn HasLineOfSight;         // empty == nothing is ever visible (safe default)
};

struct AiGroupInfo
{
    int Total = 1;
    int Alive = 1;
    int ThreatCount = 1;                  // hostile persons currently near the group
};

struct AiAwareness
{
    int64_t TargetId = 0;
    float Level = 0.f;                    // 0..100
    float ReactionLeft = -1.f;            // <0: not started
    bool Seen = false;                    // visually seen this tick
    bool Perceived = false;               // seen or heard this tick
};

struct AiNpc
{
    int64_t Id = 0;
    AiArchetype Arch = AiArchetype::Marauder;
    int ImpairmentLevel = 0;              // flavour only, see header comment
    Vec3 Pos;
    Vec3 Home;
    std::vector<Vec3> PatrolPoints;
    size_t PatrolIndex = 0;
    float HealthFraction = 1.f;

    AiState State = AiState::Idle;
    double StateTime = 0;
    double CalmTime = 0;                  // seconds without awareness >= Alert
    double UnseenTime = 0;                // seconds since the focus target was last seen
    double FireCooldown = 0;
    double SpeechCooldown = 0;
    int64_t FocusTarget = 0;
    Vec3 LastKnown;
    bool HasLastKnown = false;
    std::vector<AiAwareness> Awareness;
    std::vector<int64_t> AttackedBy;
    std::vector<int64_t> Warned;
};

enum class AiCommandType : uint8_t { MoveTo = 0, Shoot, Speak, Flee, ReportAllies };
enum class SpeechKind : uint8_t { Warn = 0, AskHelp, CallAllies };

struct AiCommand
{
    AiCommandType Type = AiCommandType::MoveTo;
    Vec3 Pos;
    int64_t TargetId = 0;
    bool Run = false;
    float Accuracy = 0.f;
    SpeechKind Speech = SpeechKind::Warn;
    bool operator==(const AiCommand& o) const
    {
        return Type == o.Type && Pos.x == o.Pos.x && Pos.y == o.Pos.y && Pos.z == o.Pos.z && TargetId == o.TargetId &&
               Run == o.Run && Accuracy == o.Accuracy && Speech == o.Speech;
    }
};

enum class HostilityDecision : uint8_t { Ignore = 0, Avoid, AskHelp, Warn, Attack, Retreat, Flee };

// Everything the hostility rule may look at. There is deliberately no health-condition / impairment field.
struct HostilityInput
{
    AiArchetype Archetype = AiArchetype::Marauder;
    bool TargetInSafeZone = false;
    bool TargetViolatedLaw = false;
    bool TargetAttackedUs = false;
    bool TargetAttackedOthers = false;
    bool TargetArmed = false;
    bool Warned = false;
    bool Cornered = false;
    bool GroupHasAdvantage = true;
    bool GroupShouldRetreat = false;
    bool Wounded = false;
    float Distance = 0.f;
    bool Night = false;
};
HostilityDecision DecideHostility(const HostilityInput& in);

float EffectiveSightRange(const ArchetypeDef& a, bool night, TargetStance stance);
float FootstepRadius(MoveKind m);
float GunshotRadius(SoundKind k);

void NotifyAttacked(AiNpc& npc, int64_t attackerId);

// Advances one NPC by dt seconds and returns the commands the engine must execute.
std::vector<AiCommand> TickNpc(AiNpc& npc, const AiGroupInfo& group, const AiWorldView& view, double dt, Rng& rng);
} // namespace Scavar
