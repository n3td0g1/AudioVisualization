// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SoundVisualizations : ModuleRules
	{
		public SoundVisualizations(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("SoundVisualizations/Private");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target, "Kiss_FFT");
		}
	}
}