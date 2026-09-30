#include "MRSuitViewerModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMRSuitViewer);

// The game name must match the .uproject name ("Marvel"): packaged builds use it to locate their content.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, MRSuitViewer, "Marvel");
