#include "SSXTrickyRebuild.h"
#include "Input/SSXDualSense.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogSSX);

class FSSXTrickyRebuildModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override { SSXPad::StartSonyPadSupport(); }
	virtual void ShutdownModule() override { SSXPad::StopSonyPadSupport(); }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FSSXTrickyRebuildModule, SSXTrickyRebuild, "SSXTrickyRebuild");
