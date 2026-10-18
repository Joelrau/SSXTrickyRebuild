using System.IO;
using UnrealBuildTool;

public class SSXTrickyRebuild : ModuleRules
{
	public SSXTrickyRebuild(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		PublicIncludePaths.Add(ModuleDirectory);
		PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore", "MediaAssets", "Json", "ApplicationCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "AudioExtensions" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });   // Particles/SSXSpriteRenderer scene proxy
		PrivateDependencyModuleNames.Add("ImageCore");   // World/SSXPropAnimation crowd atlas (FImage)
		// Packaged game: stage the data read from disk at run time as loose files. SimData/ stays at <Package>/SSXTrickyRebuild/SimData; the repo-root folders (converted/, extracted/, unpacked/) go under <Package>/SSXTrickyRebuild/GameData/, which SSXFrontEnd::RepoRoot() returns in cooked builds. The data comes from the user's own disc and is never committed.
		if (Target.Type == TargetType.Game)
		{
			string Repo = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", ".."));
			// Only what the game module reads (the *_reference / replay / *.csv files are test fixtures, *.instances.bin an
			// editor-import input).
			string[] SimData = {
				"*.patches.bin", "*.rails.bin", "*.instcol.bin", "*.race.bin", "*.effects.bin", "*.fog.bin", "*.lights.bin",
				"*.propanim.bin", "*.propsnd.bin", "*.terrainlod.bin",
				"anim_clips.bin", "anim_table.bin", "riders.bin", "surfaces.bin",
				"fe_riders.json", "heats_data.json", "intro_dialogue.json", "world_track_table.json",
				"cameras/*.json",
			};
			foreach (string Rel in SimData)
			{
				RuntimeDependencies.Add("$(ProjectDir)/SimData/" + Rel, StagedFileType.NonUFS);
			}
			string[] RepoData = {
				"converted/audio/...",              // manifest, pathfinder music, speech, crowd, INF configs
				"converted/frontend/*.json",        // FE tables, sprites, cameras, strings, progression
				"converted/frontend/fonts/...",
				"converted/frontend/loading/...",
				"converted/frontend/video/*.m2ts",  // FMV (Media Framework) + its separate audio track
				"converted/frontend/video/*.wav",
				"converted/textures/...",           // FE sprites, portraits, particles, crowd, prop flipbooks, rider textures
				"converted/characters/envr.png",    // rider chrome (the per-rider glTF folders are editor-import inputs)
				"converted/characters/specmap/...", // rider specular maps
				"converted/hud/...",
				"extracted/DATA/LANG/...",
				"extracted/DATA/FONTS/...",
				"extracted/DATA/CONFIG/...",
				"extracted/DATA/TUTORIAL/...",
				"extracted/DATA/VIDEO/*.SSS",       // venue / prop billboard movies
				"extracted/DATA/VIDEO/TB/...",      // trick book clips
				"unpacked/TUTORIAL/...",            // tutorial .inp replays
			};
			foreach (string Rel in RepoData)
			{
				string SourceDir = Path.Combine(Repo, Path.GetDirectoryName(Rel.Replace("/...", "/x")));
				if (Directory.Exists(SourceDir))
				{
					RuntimeDependencies.Add("$(ProjectDir)/GameData/" + Rel, Path.Combine(Repo, Rel), StagedFileType.NonUFS);
				}
			}
		}
		// Input/SSXDualSense: Sony controllers read directly over HID on Windows.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.AddRange(new string[] { "setupapi.lib", "hid.lib" });
		}
	}
}
