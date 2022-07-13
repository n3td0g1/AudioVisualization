// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioVisualization : ModuleRules
{
	public AudioVisualization(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay" });
	}
}
