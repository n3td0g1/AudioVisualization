// Georgy Treshchev 2022.

#include "ImportedSoundWave.h"
#include "RuntimeAudioImporterDefines.h"

#include "Async/Async.h"

void UImportedSoundWave::BeginDestroy()
{
	UE_LOG(LogRuntimeAudioImporter, Warning, TEXT("Imported sound wave ('%s') data will be cleared because it is being unloaded"), *GetName());

	Super::BeginDestroy();
}

void UImportedSoundWave::ReleaseMemory()
{
	UE_LOG(LogRuntimeAudioImporter, Warning, TEXT("Releasing memory for the sound wave '%s'"), *GetName());

	PCMBufferInfo.PCMData.Empty();

	PCMBufferInfo.~FPCMStruct();
}

bool UImportedSoundWave::RewindPlaybackTime(const float PlaybackTime)
{
	if (PlaybackTime > Duration)
	{
		UE_LOG(LogRuntimeAudioImporter, Warning, TEXT("Unable to rewind playback time for the imported sound wave '%s' by time '%f' because total length is '%f'"), *GetName(), PlaybackTime, Duration);
		return false;
	}

	return ChangeCurrentFrameCount(PlaybackTime * SampleRate);
}

bool UImportedSoundWave::ChangeCurrentFrameCount(const uint32 NumOfFrames)
{
	if (NumOfFrames < 0 || NumOfFrames > PCMBufferInfo.PCMNumOfFrames)
	{
		UE_LOG(LogRuntimeAudioImporter, Error, TEXT("Cannot change the current frame for the imported sound wave '%s' to frame '%d' because the total number of frames is '%d'"), *GetName(), NumOfFrames, PCMBufferInfo.PCMNumOfFrames);
		return false;
	}

	CurrentNumOfFrames = NumOfFrames;

	// Setting "PlaybackFinishedBroadcast" to "false" in order to re-broadcast the "OnAudioPlaybackFinished" delegate again
	PlaybackFinishedBroadcast = false;

	return true;
}

float UImportedSoundWave::GetPlaybackTime() const
{
	return static_cast<float>(CurrentNumOfFrames) / SampleRate;
}

float UImportedSoundWave::GetDurationConst() const
{
	return Duration;
}

bool UImportedSoundWave::GetRenderData(int32 Channel, float StartTime, float TimeLength, int32 AmplitudeBuckets,
	TArray<float>& OutAmplitudes)
{
	if(Channel < 0)
	{
		return false;
	}
	if(Channel >= NumChannels)
	{
		return false;
	}

	if(StartTime < 0.0f || TimeLength <= 0.0f)
	{
		return false;
	}

	if(PCMBufferInfo.PCMNumOfFrames < 2)
	{
		return false;
	}

	if(AmplitudeBuckets < 1)
	{
		return false;
	}

	uint32 StartFrame = StartTime * SampleRate;
	StartFrame = FMath::Min(StartFrame, PCMBufferInfo.PCMNumOfFrames - 2);
	
	uint32 EndFrame = (StartTime + TimeLength) * SampleRate;
	EndFrame = FMath::Clamp(EndFrame, StartFrame + 1,  PCMBufferInfo.PCMNumOfFrames - 1);

	const uint32 DeltaFrames =  EndFrame - StartFrame;

	const uint64 FrameStep = PCMBufferInfo.PCMData.GetView().Num() / PCMBufferInfo.PCMNumOfFrames;

	OutAmplitudes.AddZeroed(AmplitudeBuckets);
	constexpr float BitDepth = 16.0f;

	const float Divisor = FMath::Pow(2.0f, BitDepth - 1.0f);

	for(uint32 i = 0; i < static_cast<uint32>(AmplitudeBuckets); ++i)
	{
		const float Percent = i / static_cast<float>(AmplitudeBuckets - 1);
		const int32 Frame = DeltaFrames * Percent + StartFrame;
		
		const uint64 PCMIndex = FrameStep * static_cast<uint64>(Frame);

		auto F1 = PCMBufferInfo.PCMData.GetView()[PCMIndex];
		auto F2 = PCMBufferInfo.PCMData.GetView()[PCMIndex + 1];

		const auto FrameData = (F1 << 8) | F2;
		
		const float AmplitudeValue = FrameData / Divisor;

		OutAmplitudes[i] = AmplitudeValue * 128.0f;

		if(AmplitudeValue > 0.0f)
		{
			EndFrame = EndFrame;
		}
	}

	return true;
}


float UImportedSoundWave::GetDuration()
#if ENGINE_MAJOR_VERSION >= 5
const
#endif
{
	return GetDurationConst();
}

float UImportedSoundWave::GetPlaybackPercentage() const
{
	return (GetPlaybackTime() / GetDurationConst()) * 100;
}

bool UImportedSoundWave::IsPlaybackFinished()
{
	return GetPlaybackPercentage() == 100 && PCMBufferInfo.PCMData.GetView().GetData() != nullptr && PCMBufferInfo.PCMNumOfFrames > 0 && PCMBufferInfo.PCMData.GetView().Num() > 0;
}

int32 UImportedSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	// Ensure there is enough number of frames. Lack of frames means audio playback has finished
	if (static_cast<uint32>(CurrentNumOfFrames) >= PCMBufferInfo.PCMNumOfFrames)
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			if (!PlaybackFinishedBroadcast)
			{
				UE_LOG(LogRuntimeAudioImporter, Warning, TEXT("Playback of the sound wave '%s' has been completed"), *GetName());

				PlaybackFinishedBroadcast = true;

				if (OnAudioPlaybackFinishedNative.IsBound())
				{
					OnAudioPlaybackFinishedNative.Broadcast();
				}
				
				if (OnAudioPlaybackFinished.IsBound())
				{
					OnAudioPlaybackFinished.Broadcast();
				}
			}
		});

		return 0;
	}

	// Getting the remaining number of samples if the required number of samples is greater than the total available number
	if (static_cast<uint32>(CurrentNumOfFrames) + static_cast<uint32>(NumSamples) / static_cast<uint32>(NumChannels) >= PCMBufferInfo.PCMNumOfFrames)
	{
		NumSamples = (PCMBufferInfo.PCMNumOfFrames - CurrentNumOfFrames) * NumChannels;
	}

	// Retrieving a part of PCM data
	uint8* RetrievedPCMData = PCMBufferInfo.PCMData.GetView().GetData() + (CurrentNumOfFrames * NumChannels * sizeof(float));
	const int32 RetrievedPCMDataSize = NumSamples * sizeof(float);

	// Ensure we got a valid PCM data
	if (RetrievedPCMDataSize <= 0 || RetrievedPCMData == nullptr)
	{
		return 0;
	}

	// Filling in OutAudio array with the retrieved PCM data
	OutAudio = TArray<uint8>(RetrievedPCMData, RetrievedPCMDataSize);

	// Increasing CurrentFrameCount for correct iteration sequence
	CurrentNumOfFrames = CurrentNumOfFrames + (NumSamples / NumChannels);

	AsyncTask(ENamedThreads::GameThread, [this, RetrievedPCMData, RetrievedPCMDataSize = NumSamples]()
	{
		if (OnGeneratePCMDataNative.IsBound())
		{
			OnGeneratePCMDataNative.Broadcast(TArray<float>(reinterpret_cast<float*>(RetrievedPCMData), RetrievedPCMDataSize));
		}
		
		if (OnGeneratePCMData.IsBound())
		{
			OnGeneratePCMData.Broadcast(TArray<float>(reinterpret_cast<float*>(RetrievedPCMData), RetrievedPCMDataSize));
		}
	});

	return NumSamples;
}

Audio::EAudioMixerStreamDataFormat::Type UImportedSoundWave::GetGeneratedPCMDataFormat() const
{
	return Audio::EAudioMixerStreamDataFormat::Type::Float;
}