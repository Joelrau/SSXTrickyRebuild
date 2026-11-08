using UnrealBuildTool;

// The game with no Enhanced look or its PC additions (SSXRenderLook::IsClassicOnly): package.bat Classic.
public class SSXTrickyClassicTarget : TargetRules
{
	public SSXTrickyClassicTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SSXTrickyRebuild");
		ProjectDefinitions.Add("SSX_CLASSIC_ONLY=1");
	}
}
