// Copyright (c) 2026. All rights reserved.

using UnrealBuildTool;

public class MultiplayerSubsystem : ModuleRules
{
	public MultiplayerSubsystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreOnline",
			"OnlineBase",
			"NetCore"
		});
	}
}
