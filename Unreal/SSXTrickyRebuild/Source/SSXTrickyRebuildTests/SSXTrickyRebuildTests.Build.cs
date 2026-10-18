using UnrealBuildTool;

// Editor-only module: the SSX.* automation tests and the replay harness (-SSXReplay=). Listed only by the editor
// target, so the packaged game (SSXTricky) contains no tests.
public class SSXTrickyRebuildTests : ModuleRules
{
	public SSXTrickyRebuildTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Tests keep the game module's folder layout and include each other as "Race/Tests/..." etc.
		PrivateIncludePaths.Add(ModuleDirectory);
		PrivateDependencyModuleNames.AddRange(new string[] {
			"SSXTrickyRebuild",
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "Json", "ApplicationCore", "AudioExtensions", "RenderCore", "RHI", "ImageCore",
			"MediaAssets", "Media", "MediaUtils",                      // UI/Tests/SSXMoviePlaybackTests: video sample sink
			"UnrealEd",                                                 // Harness/Tests/SmokeRaceTests: PIE control
			"MeshDescription", "StaticMeshDescription",                 // Harness/Tests/BoardOnSnowTests: terrain mesh description
		});
	}
}
