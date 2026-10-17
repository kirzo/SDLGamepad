// Copyright 2026 kirzo

using UnrealBuildTool;

public class SDLGamepad : ModuleRules
{
	public SDLGamepad(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"ApplicationCore",
			"InputDevice",
			"Projects",
			"Slate",
			"SDL3Library"
		});

		// SDL reads it with its own file IO, so it has to stay a loose file
		RuntimeDependencies.Add("$(PluginDir)/Resources/SDL_GameControllerDB/gamecontrollerdb.txt");
	}
}