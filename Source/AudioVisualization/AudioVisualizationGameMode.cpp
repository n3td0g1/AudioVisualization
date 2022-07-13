// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioVisualizationGameMode.h"
#include "AudioVisualizationCharacter.h"
#include "UObject/ConstructorHelpers.h"

AAudioVisualizationGameMode::AAudioVisualizationGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

}
