// Copyright 2026 kirzo

using System.IO;
using UnrealBuildTool;

// Official SDL3 3.4.18 release (zlib license). Not named SDL3: the engine already has a module with that name.
public class SDL3Library : ModuleRules
{
	public SDL3Library(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", "Win64", "SDL3.lib"));
			PublicDelayLoadDLLs.Add("SDL3.dll");
			RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/SDL3/Win64/SDL3.dll");
		}
	}
}