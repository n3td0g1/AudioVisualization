// Georgy Treshchev 2022.

#pragma once

#include "CoreMinimal.h"
#include "Components/Image.h"
#include "ImportedSoundWaveVisualizer.generated.h"

class UImportedSoundWave;
class UDynamicTexture;
class FAudioThumbnail;

/**
 * Pre-imported asset which collects MP3 audio data. Used if you want to load the MP3 file into the editor in advance
 */
UCLASS(BlueprintType, Category = "Pre Imported Sound Asset")
class RUNTIMEAUDIOIMPORTER_API UImportedSoundWaveVisualizer : public UImage
{
	GENERATED_BODY()
public:
	UImportedSoundWaveVisualizer();
	
public:

	UFUNCTION(BlueprintCallable)
	void SetAudioWave(UImportedSoundWave* SoundWave);

	UFUNCTION(BlueprintCallable)
	float GetMaxOffset() const;

	UFUNCTION(BlueprintCallable)
	void SetOffset(float NewOffset);

	UFUNCTION(BlueprintCallable)
	void AddScale(float DeltaScale);

protected:
	void UpdateTexture();
	FIntPoint GetDynamicTextureSize() const;

protected:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sound Visualizer", meta=(ClampMax = "100", ClampMin = "1"))
	float MaxScale = 20.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sound Visualizer")
	FColor ColorTint = FColor(93, 95, 136);
	
	UPROPERTY(BlueprintReadOnly)
	float StartTime = 0.0f;
	
	UPROPERTY(BlueprintReadOnly)
	float CurrentScale = 1.0f;
	float ActualScale = 1.0f;
	
	UPROPERTY(Transient)
	UDynamicTexture* DynamicTexture;
	
	UPROPERTY()
	UImportedSoundWave* CurrentSoundWave;

	TSharedPtr<class FAudioThumbnail> WaveformThumbnail;
};
