using UnrealBuildTool;

public class SSXTrickyTarget : TargetRules
{
	public SSXTrickyTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SSXTrickyRebuild");
	}
}
