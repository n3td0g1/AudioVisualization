// Georgy Treshchev 2022.

#pragma once

#include "RuntimeAudioImporterTypes.h"
#include "Sound/SoundWaveProcedural.h"
#include "ImportedSoundWave.generated.h"

/** Static delegate broadcast to track the end of audio playback */
DECLARE_MULTICAST_DELEGATE(FOnAudioPlaybackFinishedNative);

/** Dynamic delegate broadcast to track the end of audio playback */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAudioPlaybackFinished);


/** Static delegate broadcast PCM data during a generation request */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGeneratePCMDataNative, const TArray<float>&);

/** Dynamic delegate broadcast PCM data during a generation request */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGeneratePCMData, const TArray<float>&, PCMData);


/**
 * The main sound wave class used to play imported audio from the Runtime Audio Importer
 */
UCLASS(BlueprintType, Category = "Imported Sound Wave")
class RUNTIMEAUDIOIMPORTER_API UImportedSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()
public:
	//~ Begin USoundWave Interface
	virtual void BeginDestroy() override;
	//~ End USoundWave Interface

	/**
	 * Release sound wave data. It is currently recommended to call manually when the sound wave is not needed, as the garbage collector does not correctly destroy the sound wave in some cases
	 */
	UFUNCTION(BlueprintCallable, Category = "Imported Sound Wave|Miscellaneous")
	void ReleaseMemory();

	/**
	 * Rewind the sound for the specified time
	 *
	 * @param PlaybackTime How long to rewind the sound
	 * @return Whether the sound was rewound or not
	 */
	UFUNCTION(BlueprintCallable, Category = "Imported Sound Wave|Main")
	bool RewindPlaybackTime(const float PlaybackTime);

	/**
	 * Change the current number of frames. Usually used to rewind the sound
	 *
	 * @param NumOfFrames The new number of frames from which to continue playing sound
	 * @return Whether the frames were changed or not
	 */
	bool ChangeCurrentFrameCount(const uint32 NumOfFrames);

	/**
	 * Get the current sound wave playback time, in seconds
	 */
	UFUNCTION(BlueprintCallable, Category = "Imported Sound Wave|Info")
	float GetPlaybackTime() const;

	/**
	 * Constant alternative for getting the length of the sound wave, in seconds
	 */
	UFUNCTION(BlueprintCallable, Category = "Imported Sound Wave|Info", meta = (DisplayName = "Get Duration"))
	float GetDurationConst() const;

	int32 GetSampleRate() const { return SampleRate;}

	/**
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Imported Sound Wave|Info", meta = (DisplayName = "Get RenderData"))
	bool GetRenderData(int32 Channel, float StartTime, float TimeLength, int32 AmplitudeBuckets, TArray<float>& OutAmplitudes);
	
	/**
	 * Get the length of the sound wave, in seconds
	 */
	UFUNCTION(BlueprintCallable, Category = "Imported Sound Wave|Info")
	virtual float GetDuration()
#if ENGINE_MAJOR_VERSION < 5
	override;
#else
	const override;
#endif

	/**
	 * Get the current sound playback percentage, 0-100%
	 */
	UFUNCTION(BlueprintCallable, Category = "Imported Sound Wave|Info")
	float GetPlaybackPercentage() const;

	/**
	 * Sampling Rate (samples per second)
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Imported Sound Wave|Info")
	int32 SamplingRate;

	/**
	 * Check if audio playback has finished or not
	 */
	UFUNCTION(BlueprintCallable, Category = "Imported Sound Wave|Utility")
	bool IsPlaybackFinished();

	/** Bind to this delegate to know when the audio playback is finished. Recommended for C++ only */
	FOnAudioPlaybackFinishedNative OnAudioPlaybackFinishedNative;
	
	/** Bind to this delegate to know when the audio playback is finished. Recommended for Blueprints only */
	UPROPERTY(BlueprintAssignable, Category = "Imported Sound Wave|Delegates")
	FOnAudioPlaybackFinished OnAudioPlaybackFinished;

	/** Bind to this delegate to receive PCM data during playback (may be useful for analyzing audio data). Recommended for C++ only */
	FOnGeneratePCMDataNative OnGeneratePCMDataNative;
	
	/** Bind to this delegate to receive PCM data during playback (may be useful for analyzing audio data). Recommended for Blueprints only */
	UPROPERTY(BlueprintAssignable, Category = "Imported Sound Wave|Delegates")
	FOnGeneratePCMData OnGeneratePCMData;

private:
	/** Bool to control the behaviour of the OnAudioPlaybackFinished delegate */
	bool PlaybackFinishedBroadcast = false;

public:
	//~ Begin UProceduralSoundWave Interface

	/**
	 * Generating PCM data by splitting it into samples
	 *
	 * @param OutAudio Retrieved PCM data array
	 * @param NumSamples Required number of samples
	 * @return Number of retrieved samples (usually equals to NumSamples)
	 */
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

	/**
	 * Getting the format of the retrieved PCM data
	 *
	 * @note Since we are using 32-bit float, there will be no PCM transcoding in the engine, which will improve audio processing performance
	 */
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override;

	//~ End UProceduralSoundWave Interface

	/** The current number of processed frames */
	UPROPERTY(BlueprintReadOnly, Category = "Imported Sound Wave|Info")
	int32 CurrentNumOfFrames = 0;

	/** Contains PCM data for sound wave playback */
	FPCMStruct PCMBufferInfo;
};
