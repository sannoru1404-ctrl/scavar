#include "Modules/ModuleManager.h"
#include "ScavarLog.h"

DEFINE_LOG_CATEGORY(LogScavar);
DEFINE_LOG_CATEGORY(LogScavarNet);
DEFINE_LOG_CATEGORY(LogScavarCombat);
DEFINE_LOG_CATEGORY(LogScavarInventory);
DEFINE_LOG_CATEGORY(LogScavarEconomy);
DEFINE_LOG_CATEGORY(LogScavarAI);
DEFINE_LOG_CATEGORY(LogScavarPersistence);

IMPLEMENT_MODULE(FDefaultModuleImpl, ScavarCore)
