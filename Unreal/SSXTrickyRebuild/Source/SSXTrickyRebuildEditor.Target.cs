using UnrealBuildTool;

public class SSXTrickyRebuildEditorTarget : TargetRules
{
	public SSXTrickyRebuildEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SSXTrickyRebuild");
		ExtraModuleNames.Add("SSXTrickyRebuildTests");   // editor-only: SSX.* automation tests + replay harness
	}
}
