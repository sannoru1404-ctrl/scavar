#include "Modules/ModuleManager.h"

// One primary game module per project; it also provides the module entry point for monolithic (Game/Server/Client) builds.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, ScavarGameplay, "SCAVAR");
